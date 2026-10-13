#include "platform/common/method_retirement.h"

#include <android/api-level.h>
#include <android/log.h>
#include <jvmti.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string_view>
#include <tuple>

#include "platform/abi/arm64/common/libcpp_pointer_map.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/reflective_value_visitor.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/structural_policy.h"
#include "platform/symbols/method_retirement_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
// Source-declared class layouts; runtime probes select among them (kLayouts).
#include "platform/abi/arm64/android11/art_method.h"
#include "platform/abi/arm64/android11/class.h"
#include "platform/abi/arm64/android12/class.h"
#include "platform/abi/arm64/android13/class.h"
#include "platform/abi/arm64/android14/class.h"
#include "platform/abi/arm64/android15/class.h"
#include "platform/abi/arm64/android16/class.h"
#include "platform/abi/arm64/android17/class.h"

namespace artdcevm::method_retirement {
namespace {
namespace abi = artdcevm::abi::arm64;
using abi::ArtField;
using abi::ArtMethodPrefix;
using abi::ClassRedefinitionPrefix;
using abi::DexFilePrefix;
using abi::Handle;
using abi::Object;
using abi::RedefinitionDataIter;
using abi::ReflectionSourceInfo;
using abi::ReflectiveValueVisitor;
using abi::kAccJavaFlagsMask;
using abi::kAccObsoleteMethod;

// Profile-independent retirement state. Entries are real old members whose
// classes are retained by RetainRoots, so the sets only grow. Guards reach the
// shared lock only after a retirement exists and the cheap per-method checks pass.
std::atomic<bool> any_retired{false};
std::atomic<bool> obsolete_flag_trusted{true};
std::shared_mutex retired_mutex;
std::set<const void*> retired_methods;
std::set<jmethodID> retired_ids;
std::mutex roots_mutex;
std::vector<jobject> retained_roots;
std::atomic<uint64_t> jni_retirement_guards{0};
std::mutex state_mutex;
std::string mapping_state = "unavailable";
bool available = false;
// Written by the mapping hook and read by the visitor within one Update call.
thread_local bool in_update = false;
thread_local std::set<const void*> copied_resolution_changes;
thread_local std::vector<const void*> retired_in_update;

void Report(std::string state) {
  std::lock_guard<std::mutex> lock(state_mutex);
  mapping_state = std::move(state);
}
bool AnyRetired() { return any_retired.load(std::memory_order_acquire); }
bool ContainsMethod(const void* method) {
  if (!AnyRetired()) return false;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  return retired_methods.count(method) != 0;
}
// Never dereferences the ID, so CheckJNI can still diagnose invalid IDs.
bool ContainsId(jmethodID id) {
  if (!AnyRetired()) return false;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  return retired_ids.count(id) != 0 ||
      ((reinterpret_cast<uintptr_t>(id) & 1) == 0 && retired_methods.count(id) != 0);
}
void Retire(const void* method) {
  {
    std::unique_lock<std::shared_mutex> lock(retired_mutex);
    retired_methods.insert(method);
  }
  any_retired.store(true, std::memory_order_release);
  retired_in_update.push_back(method);
}
void RecordId(const void* method, jmethodID id) {
  std::unique_lock<std::shared_mutex> lock(retired_mutex);
  if (retired_methods.count(method)) retired_ids.insert(id);
}
// Every retired member is an old method that ART marks obsolete in the same
// commit (checked after each Update), so live methods fail one relaxed read.
bool Retired(const void* method) {
  if (!method || !AnyRetired()) return false;
  if (obsolete_flag_trusted.load(std::memory_order_relaxed) &&
      !(static_cast<const ArtMethodPrefix*>(method)->access_flags.load(std::memory_order_relaxed) &
        kAccObsoleteMethod)) return false;
  return ContainsMethod(method);
}

uint32_t Reference(const void* object) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
}
template <typename T> T* Pointer(uint64_t address) {
  return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
const DexFilePrefix* DexOf(uint32_t dex_cache) {
  const auto* cache = Pointer<abi::DexCache>(dex_cache);
  return cache ? Pointer<const DexFilePrefix>(cache->dex_file) : nullptr;
}
bool Fail(std::string* why, std::string reason) {
  *why = std::move(reason);
  return false;
}

// ART services, resolved by name in Prepare.
void (*throw_exception)(void*, const char*, const char*) = nullptr;
bool (*same_method)(void*, void*) = nullptr;
ArtField* (*find_static_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_declared_instance_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
void* (*canonical_method)(void*, uint32_t) = nullptr;
void* (*mirror_class)(ClassRedefinitionPrefix*) = nullptr;
void* original_mapping = nullptr;
void* original_update = nullptr;
void* original_instances = nullptr;
void* original_visit = nullptr;
void* original_mh = nullptr;
void* original_mh_exact = nullptr;
void* original_mh_frame = nullptr;
void* original_encode = nullptr;
void* original_encode_handle = nullptr;
// mirror/class.h: PointerSize::k64 (enum class PointerSize : uint32_t).
constexpr uint32_t kPointerSize64 = 8;

// Member mapping of one redefinition, planned while ART may still fail and
// applied at commit without reading DEX data or calling lookups.
struct MethodMapping {
  enum Kind { kKept, kRetired, kCopied };
  void* old_method;
  void* replacement;
  Kind kind;
};
struct MappingPlan {
  std::vector<std::pair<ArtField*, ArtField*>> fields;
  std::vector<MethodMapping> methods;
};

// Operations over one source-declared class layout.
struct LayoutOps {
  int android;
  bool (*verify_target)(void* klass, const evolution::ClassSchema& live, uint32_t class_def_access,
                        std::string* why);
  bool (*verify_iter)(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                      std::string* why);
  bool (*plan)(const RedefinitionDataIter& data, MappingPlan* plan, std::string* why);
};

template <typename Class, typename Method> struct Layout {
  template <typename T> static abi::LengthPrefixedArray<T>* Members(uint64_t address) {
    return Pointer<abi::LengthPrefixedArray<T>>(address);
  }

  // The class flags locate access_flags_, which follows the native member
  // arrays, so a wrong array layout fails here before any array is read.
  static bool VerifyAccess(const Class& klass, uint32_t class_def_access, std::string* why) {
    constexpr uint32_t kValidClass = 0x0001 | 0x0010 | 0x0020 | 0x0400 | 0x1000 | 0x4000;
    constexpr uint32_t kValidInterface = 0x0001 | 0x0200 | 0x0400 | 0x1000 | 0x2000;
    const uint32_t expected = class_def_access &
        ((class_def_access & evolution::kAccInterface) ? kValidInterface : kValidClass);
    if ((klass.access_flags & kAccJavaFlagsMask) != expected)
      return Fail(why, "class access flags differ");
    return true;
  }

  // Fields ART finds by name must lie in this layout's arrays before any
  // array length is read; each array then holds exactly the JVMTI fields.
  static bool VerifyFields(Class* klass, const evolution::ClassSchema& live, std::string* why) {
    const auto arrays = FieldArrays(*klass);
    std::array<bool, std::tuple_size_v<decltype(arrays)>> confirmed{};
    std::vector<std::pair<ArtField*, const evolution::Member*>> found;
    for (const auto& member : live.fields) {
      ArtField* field = (member.IsStatic() ? find_static_field : find_declared_instance_field)(
          klass, member.name, member.descriptor);
      if (!field) return Fail(why, "ART did not find field " + member.name);
      bool placed = false;
      for (size_t i = 0; i < arrays.size() && !placed; ++i) {
        const uintptr_t first = static_cast<uintptr_t>(arrays[i]) + sizeof(uint32_t);
        const uintptr_t at = reinterpret_cast<uintptr_t>(field);
        placed = arrays[i] && at >= first && at - first < size_t{UINT16_MAX} * sizeof(ArtField) &&
            (at - first) % sizeof(ArtField) == 0;
        if (placed) confirmed[i] = true;
      }
      if (!placed) return Fail(why, "field " + member.name + " is outside the declared arrays");
      found.emplace_back(field, &member);
    }
    // An unconfirmed array must be absent: its slot may be another member.
    size_t count = 0;
    for (size_t i = 0; i < arrays.size(); ++i) {
      if (!arrays[i]) continue;
      if (!confirmed[i]) return Fail(why, "unconfirmed field array");
      count += Members<ArtField>(arrays[i])->length;
    }
    if (count != live.fields.size()) return Fail(why, "field count differs");
    const DexFilePrefix* dex = DexOf(klass->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    for (const auto& [field, member] : found) {
      bool inside = false;
      for (uint64_t array : arrays) inside |= array && Members<ArtField>(array)->Contains(field);
      evolution::Member identity;
      std::string error;
      if (!inside || field->declaring_class != Reference(klass) ||
          (field->access_flags & kAccJavaFlagsMask) != member->access ||
          !evolution::ReadFieldIdentity(view, field->dex_field_index, &identity, &error) ||
          identity.name != member->name || identity.descriptor != member->descriptor)
        return Fail(why, "field declaration differs: " + member->name);
    }
    return true;
  }

  static bool VerifyMethods(Class* klass, const evolution::ClassSchema& live, std::string* why) {
    std::set<std::tuple<std::string, std::string, uint32_t>> expected;
    for (const auto& member : live.methods)
      expected.emplace(member.name, member.descriptor, member.access);
    auto* methods = Members<Method>(klass->methods);
    if (!methods) return expected.empty() ? true : Fail(why, "class has no methods array");
    const DexFilePrefix* dex = DexOf(klass->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    size_t declared = 0;
    for (uint32_t i = 0; i < methods->length; ++i) {
      Method* method = methods->At(i);
      if (method->declaring_class != Reference(klass)) continue;
      const uint32_t flags = method->access_flags.load(std::memory_order_relaxed);
      evolution::Member identity;
      std::string error;
      if ((flags & kAccObsoleteMethod) ||
          !evolution::ReadMethodIdentity(view, method->dex_method_index, &identity, &error) ||
          !expected.count({identity.name, identity.descriptor,
                           structural_policy::MethodModifiers(flags)}))
        return Fail(why, "method declaration differs");
      ++declared;
    }
    if (declared != expected.size()) return Fail(why, "method count differs");
    return true;
  }

  static bool VerifyTarget(void* klass, const evolution::ClassSchema& live,
                           uint32_t class_def_access, std::string* why) {
    auto* declared = static_cast<Class*>(klass);
    return declared && VerifyAccess(*declared, class_def_access, why) &&
        VerifyFields(declared, live, why) && VerifyMethods(declared, live, why);
  }

  // After ART allocated the replacement classes: the holder's class slots
  // must name the target and a class loaded from the incoming DEX.
  static bool VerifyIter(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                         std::string* why) {
    const auto* old_classes = data.Classes(true);
    const auto* new_classes = data.Classes(false);
    // A non-structural redefinition keeps its class and fills neither slot.
    if (!old_classes && !new_classes) return true;
    if (!old_classes || !new_classes || old_classes->length < 1 ||
        old_classes->length != new_classes->length)
      return Fail(why, "redefinition class slots are malformed");
    if (old_classes->Get<void>(0) != mirror_class(self))
      return Fail(why, "redefinition slot does not hold the target class");
    for (int32_t i = 0; i < new_classes->length; ++i) {
      auto* old_class = old_classes->Get<Class>(i);
      auto* new_class = new_classes->Get<Class>(i);
      if (!old_class || !new_class) return Fail(why, "redefinition class slot is empty");
      if (i == 0 && DexOf(new_class->dex_cache) != self->dex_file)
        return Fail(why, "replacement class does not use the incoming DEX");
      auto* methods = Members<Method>(new_class->methods);
      for (uint32_t j = 0; methods && j < methods->length; ++j)
        if (!methods->At(j)->declaring_class) return Fail(why, "replacement method is unlinked");
    }
    return true;
  }

  // Field deletion and type/storage changes are rejected, so every old field
  // has a declared replacement found by name and type, as in ART's mapping.
  static bool PlanFields(Class* old_class, Class* new_class, MappingPlan* plan, std::string* why) {
    const DexFilePrefix* dex = DexOf(old_class->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "old class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    for (uint64_t array : FieldArrays(*old_class)) {
      auto* fields = Members<ArtField>(array);
      for (uint32_t i = 0; fields && i < fields->length; ++i) {
        ArtField* field = fields->At(i);
        evolution::Member identity;
        std::string error;
        if (!evolution::ReadFieldIdentity(view, field->dex_field_index, &identity, &error))
          return Fail(why, "field identity unavailable: " + error);
        ArtField* replacement = ((field->access_flags & evolution::kAccStatic)
                                     ? find_static_field : find_declared_instance_field)(
            new_class, identity.name, identity.descriptor);
        if (!replacement) return Fail(why, "no replacement for field " + identity.name);
        plan->fields.emplace_back(field, replacement);
      }
    }
    return true;
  }

  static void PlanMethods(Class* old_class, Class* new_class, MappingPlan* plan) {
    auto* old_methods = Members<Method>(old_class->methods);
    auto* new_methods = Members<Method>(new_class->methods);
    for (uint32_t i = 0; old_methods && i < old_methods->length; ++i) {
      Method* old_method = old_methods->At(i);
      const bool declared = old_method->declaring_class == Reference(old_class);
      void* replacement = nullptr;
      for (uint32_t j = 0; new_methods && j < new_methods->length; ++j) {
        Method* candidate = new_methods->At(j);
        // Removing an override may expose an inherited/default copied member
        // with the same identity. That does not preserve the old declaration.
        if (declared && candidate->declaring_class != Reference(new_class)) continue;
        if (same_method(old_method, candidate)) { replacement = candidate; break; }
      }
      if (replacement) {
        plan->methods.push_back({old_method, replacement, MethodMapping::kKept});
      } else if (declared) {
        // The policy accepted this declaration's removal. Keep the real old
        // member so ART's visitor invariants hold; guards throw before dispatch.
        plan->methods.push_back({old_method, old_method, MethodMapping::kRetired});
      } else {
        // A copied default/miranda disappears when the re-linked class gains a
        // public implementation of its slot. The interface declaration still
        // exists: map to ART's canonical method and clear cached resolutions
        // rather than retiring generated child storage.
        plan->methods.push_back({old_method, canonical_method(old_method, kPointerSize64),
                                 MethodMapping::kCopied});
      }
    }
  }

  static bool Plan(const RedefinitionDataIter& data, MappingPlan* plan, std::string* why) {
    auto* old_classes = data.Classes(true);
    auto* new_classes = data.Classes(false);
    if (!old_classes || !new_classes) return true;  // Non-structural: ART keeps the class.
    for (int32_t i = 0; i < old_classes->length; ++i) {
      auto* old_class = old_classes->template Get<Class>(i);
      auto* new_class = new_classes->template Get<Class>(i);
      if (!PlanFields(old_class, new_class, plan, why)) return false;
      PlanMethods(old_class, new_class, plan);
    }
    return true;
  }

  static constexpr LayoutOps Ops(int android) { return {android, VerifyTarget, VerifyIter, Plan}; }
};

// One entry per AOSP release whose declarations are in platform/abi/arm64.
const LayoutOps kLayouts[] = {
    Layout<abi::android11::Class, abi::android11::ArtMethod>::Ops(11),
    Layout<abi::android12::Class, abi::ArtMethod>::Ops(12),
    Layout<abi::android13::Class, abi::ArtMethod>::Ops(13),
    Layout<abi::android14::Class, abi::ArtMethod>::Ops(14),
    Layout<abi::android15::Class, abi::ArtMethod>::Ops(15),
    Layout<abi::android16::Class, abi::ArtMethod>::Ops(16),
    Layout<abi::android17::Class, abi::ArtMethod>::Ops(17)};
constexpr size_t kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);
using LayoutMask = uint32_t;
static_assert(kLayoutCount <= 32);

// The ART module is never older than the platform release, so older layouts
// are not candidates. The platform's own release is tried first, then newer
// ones; all of them share the ArtMethod stride a wrong guess could over-read.
int PlatformRelease() {
  const int api = android_get_device_api_level();
  return api <= 30 ? 11 : api <= 32 ? 12 : api - 20;
}
std::vector<size_t> CandidateOrder() {
  const int platform = PlatformRelease();
  std::vector<size_t> order;
  for (size_t i = 0; i < kLayoutCount; ++i)
    if (kLayouts[i].android == platform) order.push_back(i);
  for (size_t i = 0; i < kLayoutCount; ++i)
    if (kLayouts[i].android > platform) order.push_back(i);
  return order;
}

struct Transaction {
  Transaction* previous = nullptr;
  LayoutMask layouts = ~LayoutMask{0};
  bool probed = false;
  bool requires_mapping = false;
  std::map<int32_t, MappingPlan> plans;  // By RedefinitionDataIter index.
};
thread_local Transaction* transaction = nullptr;

// The first layout in candidate order that every probe of this transaction accepted.
const LayoutOps* Selected() {
  if (!transaction || !transaction->probed) return nullptr;
  for (size_t i : CandidateOrder())
    if (transaction->layouts & (LayoutMask{1} << i)) return &kLayouts[i];
  return nullptr;
}

template <typename F> void* Fn(F function) { return reinterpret_cast<void*>(function); }
template <typename F> void** Out(F* function) { return reinterpret_cast<void**>(function); }

void ThrowRetired(void* self) {
  throw_exception(self, "Ljava/lang/NoSuchMethodError;", "method retired by structural redefinition");
}

// Commit: apply the plan made before ART's no-failure point, or keep ART's
// own mapping for a transaction that has none.
void CollectMappings(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                     void* method_map, void* field_map) {
  const MappingPlan* plan = nullptr;
  if (transaction) {
    const auto found = transaction->plans.find(data.index);
    if (found != transaction->plans.end()) plan = &found->second;
  }
  if (!plan || !abi::LibcppV1PointerMap<abi::ArtMethod>::Empty(method_map) ||
      !abi::LibcppV1PointerMap<ArtField>::Empty(field_map)) {
    if (transaction && transaction->requires_mapping)
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping unavailable at commit");
    using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&, void*, void*);
    reinterpret_cast<Function>(original_mapping)(self, data, method_map, field_map);
    return;
  }
  for (const auto& [field, replacement] : plan->fields)
    abi::LibcppV1PointerMap<ArtField>::Set(field_map, field, replacement);
  for (const auto& entry : plan->methods) {
    if (entry.kind == MethodMapping::kRetired) Retire(entry.old_method);
    if (entry.kind == MethodMapping::kCopied) copied_resolution_changes.insert(entry.old_method);
    abi::LibcppV1PointerMap<abi::ArtMethod>::Set(
        method_map, static_cast<abi::ArtMethod*>(entry.old_method),
        static_cast<abi::ArtMethod*>(entry.replacement));
  }
}

// Last ART step that may fail the transaction: confirm the holder slots with
// every remaining layout, then plan the mapping with the selected one.
bool CollectAndCreateNewInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) {
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataIter*);
  if (!reinterpret_cast<Function>(original_instances)(self, data)) return false;
  if (!transaction || !transaction->probed) return true;
  std::string why;
  for (size_t i = 0; i < kLayoutCount; ++i) {
    const LayoutMask bit = LayoutMask{1} << i;
    if ((transaction->layouts & bit) && !kLayouts[i].verify_iter(self, *data, &why))
      transaction->layouts &= ~bit;
  }
  if (const LayoutOps* layout = Selected()) {
    MappingPlan plan;
    if (layout->plan(*data, &plan, &why)) {
      // Later redefinitions of this batch are planned with the same layout.
      transaction->layouts = LayoutMask{1} << (layout - kLayouts);
      transaction->plans[data->index] = std::move(plan);
      return true;
    }
    transaction->layouts = 0;
    transaction->plans.clear();
  }
  Report("rejected: " + why);
  if (!transaction->requires_mapping) return true;
  self->driver->result = JVMTI_ERROR_INTERNAL;
  __android_log_print(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping probe failed: %s", why.c_str());
  return false;
}

void Update(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) {
  struct Scope {
    bool previous = in_update;
    std::set<const void*> previous_copied = std::move(copied_resolution_changes);
    Scope() { in_update = true; copied_resolution_changes.clear(); retired_in_update.clear(); }
    ~Scope() {
      in_update = previous;
      copied_resolution_changes = std::move(previous_copied);
    }
  } scope;
  using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  reinterpret_cast<Function>(original_update)(self, data);
  // ART has now marked every old method obsolete; the guards' fast path
  // relies on that flag, so stop using it if this runtime disagrees.
  for (const void* method : retired_in_update) {
    if (!(static_cast<const ArtMethodPrefix*>(method)->access_flags.load() & kAccObsoleteMethod)) {
      obsolete_flag_trusted.store(false);
      __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", "obsolete flag not observed; guards use lookups");
      break;
    }
  }
  retired_in_update.clear();
}

class RetirementVisitor final : public ReflectiveValueVisitor {
 public:
  explicit RetirementVisitor(ReflectiveValueVisitor* original) : original_(original) {}
  abi::ArtMethod* VisitMethod(abi::ArtMethod* method, const ReflectionSourceInfo& info) override {
    const bool retired = ContainsMethod(method);
    if (info.type == abi::kSourceDexCacheResolvedMethod &&
        (retired || copied_resolution_changes.count(method))) return nullptr;
    if (retired && info.type == abi::kSourceJniMethodId)
      RecordId(method, reinterpret_cast<const abi::JniMethodIdSourceInfo&>(info).id);
    return original_->VisitMethod(method, info);
  }
  ArtField* VisitField(ArtField* field, const ReflectionSourceInfo& info) override {
    return original_->VisitField(field, info);
  }
 private:
  ReflectiveValueVisitor* original_;
};
void Visit(void* runtime, ReflectiveValueVisitor* visitor) {
  using Function = void (*)(void*, ReflectiveValueVisitor*);
  if (!in_update) { reinterpret_cast<Function>(original_visit)(runtime, visitor); return; }
  RetirementVisitor retirement(visitor);
  reinterpret_cast<Function>(original_visit)(runtime, &retirement);
}

// java.lang.invoke.MethodHandle.artFieldOrMethod, located through ART's own
// field table. Transformer handles carry no ArtMethod, so it is only looked up.
std::atomic<int64_t> handle_target_offset{-1};
bool RetiredHandle(Handle handle) {
  if (!AnyRetired()) return false;
  const auto* object = handle.Get<Object>();
  if (!object) return false;
  int64_t offset = handle_target_offset.load(std::memory_order_acquire);
  if (offset < 0) {
    const ArtField* field = find_instance_field(Pointer<void>(object->klass), "artFieldOrMethod", "J");
    if (!field) {
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "MethodHandle.artFieldOrMethod not found");
      return false;
    }
    offset = field->offset;
    handle_target_offset.store(offset, std::memory_order_release);
  }
  uint64_t target = 0;
  std::memcpy(&target, reinterpret_cast<const uint8_t*>(object) + offset, sizeof(target));
  return ContainsMethod(Pointer<const void>(target));
}
bool HandleCall(void* original, void* self, void* frame, Handle method, Handle type,
                const void* operands, uint64_t* result) {
  if (RetiredHandle(method)) { ThrowRetired(self); if (result) *result = 0; return false; }
  using Function = bool (*)(void*, void*, Handle, Handle, const void*, uint64_t*);
  return reinterpret_cast<Function>(original)(self, frame, method, type, operands, result);
}
bool MethodHandleInvoke(void* self, void* frame, Handle method, Handle type,
                        const void* operands, uint64_t* result) {
  return HandleCall(original_mh, self, frame, method, type, operands, result);
}
bool MethodHandleInvokeExact(void* self, void* frame, Handle method, Handle type,
                             const void* operands, uint64_t* result) {
  return HandleCall(original_mh_exact, self, frame, method, type, operands, result);
}
void MethodHandleFrame(void* self, Handle method, Handle frame) {
  if (RetiredHandle(method)) { ThrowRetired(self); return; }
  using Function = void (*)(void*, Handle, Handle);
  reinterpret_cast<Function>(original_mh_frame)(self, method, frame);
}
jmethodID Encode(void* manager, void* method) {
  using Function = jmethodID (*)(void*, void*);
  jmethodID id = reinterpret_cast<Function>(original_encode)(manager, method);
  if (Retired(method)) RecordId(method, id);
  return id;
}
struct ReflectiveHandle { void** reference; };
jmethodID EncodeHandle(void* manager, ReflectiveHandle method) {
  using Function = jmethodID (*)(void*, ReflectiveHandle);
  jmethodID id = reinterpret_cast<Function>(original_encode_handle)(manager, method);
  if (Retired(*method.reference)) RecordId(*method.reference, id);
  return id;
}
// Reflection: Method.invoke's native entry, before ART checks the receiver
// against the old declaring class. Only public JNI identifies the target.
jobject (*original_method_invoke)(JNIEnv*, jobject, jobject, jobjectArray) = nullptr;
const JNINativeInterface* jni = nullptr;  // The table in force before ours.
jclass no_such_method_error = nullptr;
jclass invocation_target_exception = nullptr;
jmethodID no_such_method_init = nullptr;
jmethodID invocation_target_init = nullptr;
constexpr char kRetiredMessage[] = "method retired by structural redefinition";

jobject MethodInvoke(JNIEnv* env, jobject method, jobject receiver, jobjectArray args) {
  if (AnyRetired()) {
    jmethodID id = jni->FromReflectedMethod(env, method);
    if (id && ContainsId(id)) {
      jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
      // Method.invoke reports a failing target as InvocationTargetException.
      jstring message = jni->NewStringUTF(env, kRetiredMessage);
      jobject cause = message ? jni->NewObject(env, no_such_method_error, no_such_method_init, message)
                              : nullptr;
      jobject wrapped = cause ? jni->NewObject(env, invocation_target_exception,
                                                invocation_target_init, cause)
                              : nullptr;
      if (wrapped) jni->Throw(env, static_cast<jthrowable>(wrapped));
      for (jobject local : {static_cast<jobject>(message), cause, wrapped})
        if (local) jni->DeleteLocalRef(env, local);
      return nullptr;
    }
  }
  return original_method_invoke(env, method, receiver, args);
}

// JNI Call*Method*: a JVMTI function-table override in front of the previous
// table (CheckJNI's when enabled), so live calls keep every original check.
// The ID is only looked up, never decoded: CheckJNI still reports invalid IDs.
bool RejectJni(JNIEnv* env, jmethodID id) {
  if (!ContainsId(id) || jni->ExceptionCheck(env)) return false;
  jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  jni->ThrowNew(env, no_such_method_error, kRetiredMessage);
  return true;
}
#define ARTDCEVM_JNI_RESULT_CALLS(T, Name)                                                       \
  T Call##Name##Method(JNIEnv* env, jobject object, jmethodID id, ...) {                         \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = jni->Call##Name##MethodV(env, object, id, args);                                  \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T Call##Name##MethodV(JNIEnv* env, jobject object, jmethodID id, va_list args) {               \
    return RejectJni(env, id) ? T() : jni->Call##Name##MethodV(env, object, id, args);           \
  }                                                                                              \
  T Call##Name##MethodA(JNIEnv* env, jobject object, jmethodID id, const jvalue* args) {         \
    return RejectJni(env, id) ? T() : jni->Call##Name##MethodA(env, object, id, args);           \
  }                                                                                              \
  T CallNonvirtual##Name##Method(JNIEnv* env, jobject object, jclass klass, jmethodID id, ...) { \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = jni->CallNonvirtual##Name##MethodV(env, object, klass, id, args);                 \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T CallNonvirtual##Name##MethodV(JNIEnv* env, jobject object, jclass klass, jmethodID id,       \
                                  va_list args) {                                                \
    return RejectJni(env, id) ? T()                                                              \
                              : jni->CallNonvirtual##Name##MethodV(env, object, klass, id, args);\
  }                                                                                              \
  T CallNonvirtual##Name##MethodA(JNIEnv* env, jobject object, jclass klass, jmethodID id,       \
                                  const jvalue* args) {                                          \
    return RejectJni(env, id) ? T()                                                              \
                              : jni->CallNonvirtual##Name##MethodA(env, object, klass, id, args);\
  }                                                                                              \
  T CallStatic##Name##Method(JNIEnv* env, jclass klass, jmethodID id, ...) {                     \
    if (RejectJni(env, id)) return T();                                                          \
    va_list args;                                                                                \
    va_start(args, id);                                                                          \
    T result = jni->CallStatic##Name##MethodV(env, klass, id, args);                             \
    va_end(args);                                                                                \
    return result;                                                                               \
  }                                                                                              \
  T CallStatic##Name##MethodV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {           \
    return RejectJni(env, id) ? T() : jni->CallStatic##Name##MethodV(env, klass, id, args);      \
  }                                                                                              \
  T CallStatic##Name##MethodA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {     \
    return RejectJni(env, id) ? T() : jni->CallStatic##Name##MethodA(env, klass, id, args);      \
  }
ARTDCEVM_JNI_RESULT_CALLS(jobject, Object)
ARTDCEVM_JNI_RESULT_CALLS(jboolean, Boolean)
ARTDCEVM_JNI_RESULT_CALLS(jbyte, Byte)
ARTDCEVM_JNI_RESULT_CALLS(jchar, Char)
ARTDCEVM_JNI_RESULT_CALLS(jshort, Short)
ARTDCEVM_JNI_RESULT_CALLS(jint, Int)
ARTDCEVM_JNI_RESULT_CALLS(jlong, Long)
ARTDCEVM_JNI_RESULT_CALLS(jfloat, Float)
ARTDCEVM_JNI_RESULT_CALLS(jdouble, Double)
#undef ARTDCEVM_JNI_RESULT_CALLS

void CallVoidMethod(JNIEnv* env, jobject object, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  jni->CallVoidMethodV(env, object, id, args);
  va_end(args);
}
void CallVoidMethodV(JNIEnv* env, jobject object, jmethodID id, va_list args) {
  if (!RejectJni(env, id)) jni->CallVoidMethodV(env, object, id, args);
}
void CallVoidMethodA(JNIEnv* env, jobject object, jmethodID id, const jvalue* args) {
  if (!RejectJni(env, id)) jni->CallVoidMethodA(env, object, id, args);
}
void CallNonvirtualVoidMethod(JNIEnv* env, jobject object, jclass klass, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  jni->CallNonvirtualVoidMethodV(env, object, klass, id, args);
  va_end(args);
}
void CallNonvirtualVoidMethodV(JNIEnv* env, jobject object, jclass klass, jmethodID id,
                               va_list args) {
  if (!RejectJni(env, id)) jni->CallNonvirtualVoidMethodV(env, object, klass, id, args);
}
void CallNonvirtualVoidMethodA(JNIEnv* env, jobject object, jclass klass, jmethodID id,
                               const jvalue* args) {
  if (!RejectJni(env, id)) jni->CallNonvirtualVoidMethodA(env, object, klass, id, args);
}
void CallStaticVoidMethod(JNIEnv* env, jclass klass, jmethodID id, ...) {
  if (RejectJni(env, id)) return;
  va_list args;
  va_start(args, id);
  jni->CallStaticVoidMethodV(env, klass, id, args);
  va_end(args);
}
void CallStaticVoidMethodV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {
  if (!RejectJni(env, id)) jni->CallStaticVoidMethodV(env, klass, id, args);
}
void CallStaticVoidMethodA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {
  if (!RejectJni(env, id)) jni->CallStaticVoidMethodA(env, klass, id, args);
}

JNINativeInterface guarded_jni;
#define ARTDCEVM_JNI_ENTRIES(Name)                                                                \
  guarded_jni.Call##Name##Method = Call##Name##Method;                                           \
  guarded_jni.Call##Name##MethodV = Call##Name##MethodV;                                         \
  guarded_jni.Call##Name##MethodA = Call##Name##MethodA;                                         \
  guarded_jni.CallNonvirtual##Name##Method = CallNonvirtual##Name##Method;                       \
  guarded_jni.CallNonvirtual##Name##MethodV = CallNonvirtual##Name##MethodV;                     \
  guarded_jni.CallNonvirtual##Name##MethodA = CallNonvirtual##Name##MethodA;                     \
  guarded_jni.CallStatic##Name##Method = CallStatic##Name##Method;                               \
  guarded_jni.CallStatic##Name##MethodV = CallStatic##Name##MethodV;                             \
  guarded_jni.CallStatic##Name##MethodA = CallStatic##Name##MethodA;

// Caches the exception classes, then overrides the JNI table. The override
// also disables ART's Core Platform API checks (JNIEnvExt::SetTableOverride),
// which apply to platform callers only. Our entries forward until a retirement.
bool InstallJniGuards(JavaVM* vm, jvmtiEnv* jvmti, std::string* failure) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
    return Fail(failure, "no JNIEnv for the JNI guards");
  auto global_class = [&](const char* name) -> jclass {
    jclass local = env->FindClass(name);
    if (!local) { env->ExceptionClear(); return nullptr; }
    auto global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
  };
  no_such_method_error = global_class("java/lang/NoSuchMethodError");
  invocation_target_exception = global_class("java/lang/reflect/InvocationTargetException");
  if (!no_such_method_error || !invocation_target_exception)
    return Fail(failure, "exception classes unavailable");
  no_such_method_init = env->GetMethodID(no_such_method_error, "<init>", "(Ljava/lang/String;)V");
  invocation_target_init = env->GetMethodID(invocation_target_exception, "<init>",
                                            "(Ljava/lang/Throwable;)V");
  if (!no_such_method_init || !invocation_target_init) {
    env->ExceptionClear();
    return Fail(failure, "exception constructors unavailable");
  }
  jniNativeInterface* current = nullptr;
  if (jvmti->GetJNIFunctionTable(&current) != JVMTI_ERROR_NONE || !current)
    return Fail(failure, "GetJNIFunctionTable failed");
  jni = current;  // Kept for the process lifetime; the override forwards to it.
  guarded_jni = *current;
  ARTDCEVM_JNI_ENTRIES(Object)
  ARTDCEVM_JNI_ENTRIES(Boolean)
  ARTDCEVM_JNI_ENTRIES(Byte)
  ARTDCEVM_JNI_ENTRIES(Char)
  ARTDCEVM_JNI_ENTRIES(Short)
  ARTDCEVM_JNI_ENTRIES(Int)
  ARTDCEVM_JNI_ENTRIES(Long)
  ARTDCEVM_JNI_ENTRIES(Float)
  ARTDCEVM_JNI_ENTRIES(Double)
  ARTDCEVM_JNI_ENTRIES(Void)
  if (jvmti->SetJNIFunctionTable(&guarded_jni) != JVMTI_ERROR_NONE)
    return Fail(failure, "SetJNIFunctionTable failed");
  return true;
}
#undef ARTDCEVM_JNI_ENTRIES
}  // namespace

void Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* note) {
  available = false;
  if (!abi::LibcppV1PointerMap<abi::ArtMethod>::Compatible() ||
      !abi::LibcppV1PointerMap<ArtField>::Compatible()) {
    *note = "method retirement unavailable: NDK pointer-map layout differs from libc++ ABI v1";
    Report("unavailable");
    return;
  }
  using namespace symbols::structural;
  using namespace symbols::method_retirement;
  struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Required required[] = {
      {kCollectNewFieldAndMethodMappings, Fn(CollectMappings), &original_mapping},
      {kCollectAndCreateNewInstances, Fn(CollectAndCreateNewInstances), &original_instances},
      {kUpdateClassStructurally, Fn(Update), &original_update},
      {kVisitReflectiveTargets, Fn(Visit), &original_visit},
      {kMethodHandleInvoke, Fn(MethodHandleInvoke), &original_mh},
      {kMethodHandleInvokeExact, Fn(MethodHandleInvokeExact), &original_mh_exact},
      {kEncodeMethodId, Fn(Encode), &original_encode},
      {kEncodeMethodIdHandle, Fn(EncodeHandle), &original_encode_handle},
      {kThrowNewException, nullptr, Out(&throw_exception)},
      {kHasSameNameAndSignature, nullptr, Out(&same_method)},
      {kFindDeclaredStaticField, nullptr, Out(&find_static_field)},
      {kFindDeclaredInstanceField, nullptr, Out(&find_declared_instance_field)},
      {kFindInstanceField, nullptr, Out(&find_instance_field)},
      {kGetCanonicalMethod, nullptr, Out(&canonical_method)},
      {kClassRedefinitionGetMirrorClass, nullptr, Out(&mirror_class)}};
  std::vector<Hook> resolved;
  for (const auto& entry : required) {
    void* address = symbols::Find(entry.symbol);
    if (!address) {
      *note = std::string("method retirement unavailable: missing ") + entry.symbol.name;
      Report("unavailable");
      return;
    }
    if (entry.replacement) resolved.push_back({address, entry.replacement, entry.original, entry.symbol.name});
    else *entry.original = address;
  }
  void* method_invoke = nullptr;
  const char* method_invoke_name = nullptr;
  for (const auto& symbol : kMethodInvokeNative) {
    if ((method_invoke = symbols::Find(symbol))) { method_invoke_name = symbol.name; break; }
  }
  if (!method_invoke) {
    *note = "method retirement unavailable: missing Method.invoke native entry";
    Report("unavailable");
    return;
  }
  resolved.push_back({method_invoke, Fn(MethodInvoke), Out(&original_method_invoke), method_invoke_name});
  if (void* frame = symbols::Find(kMethodHandleInvokeExactWithFrame))
    resolved.push_back({frame, Fn(MethodHandleFrame), &original_mh_frame,
                        kMethodHandleInvokeExactWithFrame.name});
  std::string failure;
  if (!InstallJniGuards(vm, jvmti, &failure)) {
    *note = "method retirement unavailable: " + failure;
    Report("unavailable");
    return;
  }
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  available = true;
  Report("unverified");
}
bool Available() { return available; }

bool VerifyTarget(ClassRedefinitionPrefix* self, const evolution::ClassSchema& live,
                  uint32_t class_def_access) {
  if (!available || !transaction) return false;
  void* klass = mirror_class(self);
  LayoutMask accepted = 0;
  std::string why;
  for (size_t i : CandidateOrder()) {
    std::string reason;
    if (kLayouts[i].verify_target(klass, live, class_def_access, &reason))
      accepted |= LayoutMask{1} << i;
    else if (why.empty())  // The platform's own release is probed first.
      why = "android" + std::to_string(kLayouts[i].android) + ": " + reason;
  }
  if (why.empty()) why = "no candidate layout for this platform";
  transaction->probed = true;
  transaction->layouts &= accepted;
  const LayoutOps* layout = Selected();
  Report(layout ? "verified: android" + std::to_string(layout->android) + " class layout"
                : "rejected: " + why);
  return layout != nullptr;
}

TransactionScope::TransactionScope() {
  auto* state = new Transaction;
  state->previous = transaction;
  state_ = transaction = state;
}
TransactionScope::~TransactionScope() {
  auto* state = static_cast<Transaction*>(state_);
  transaction = state->previous;
  delete state;
}
void RequireMemberMapping() {
  if (transaction) transaction->requires_mapping = true;
}

void RetainRoots(std::vector<jobject> roots) {
  std::lock_guard<std::mutex> lock(roots_mutex);
  retained_roots.insert(retained_roots.end(), roots.begin(), roots.end());
}
bool Rooted(JNIEnv* jni, jclass klass) {
  std::vector<jobject> roots;
  {
    std::lock_guard<std::mutex> lock(roots_mutex);
    roots = retained_roots;
  }
  for (jobject root : roots) if (jni->IsSameObject(root, klass)) return true;
  return false;
}
std::string MemberMappingState() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return mapping_state;
}
uint64_t JniRetirementGuardHits() {
  return jni_retirement_guards.load(std::memory_order_relaxed);
}
}  // namespace artdcevm::method_retirement
