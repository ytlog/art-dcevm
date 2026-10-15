#include "platform/common/method_retirement.h"
#include "platform/common/interface_redefinition.h"
#include "platform/common/art_hook.h"

#include <android/log.h>
#include <jvmti.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string_view>
#include <utility>

#include "platform/abi/arm64/common/libcpp_pointer_map.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/reflective_value_visitor.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/method_retirement_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"

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
// Retired fields: real old ArtFields kept alive with their obsolete classes.
// Reflective Field objects for them are remapped to the RetiredField /
// RetiredStaticField placeholders, whose ArtFields count as retired for JNI.
struct RetiredFieldInfo {
  int kind;  // Index into kFieldKinds.
  bool is_static;
};
std::atomic<bool> any_retired_field{false};
std::map<const void*, RetiredFieldInfo> retired_fields;
std::set<jfieldID> retired_field_ids;
std::set<const void*> placeholder_fields;
bool field_retirement_available = false;
bool field_ids_hooked = false;
bool available = false;
// Written by the mapping hook and read by the visitor within one Update call.
thread_local bool in_update = false;
thread_local std::set<const void*> copied_resolution_changes;
thread_local std::vector<const void*> retired_in_update;
// Old classes' field-array slots swapped for the copy phase of one Update.
thread_local std::vector<std::pair<uint64_t*, uint64_t>> swapped_field_arrays;
thread_local std::vector<std::pair<uint32_t*, uint32_t>> swapped_superclasses;
// Old native methods whose new counterpart is gone or no longer native.
struct HiddenNative {
  std::atomic<uint32_t>* access_flags;
  void** quick_entry;
};
thread_local std::vector<std::pair<HiddenNative, void*>> hidden_natives;
// JVMTI-requested field IDs and their ArtFields, while resolving placeholders.
thread_local std::map<jfieldID, const void*>* field_capture = nullptr;

using class_layout::Report;
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
// Primitive kinds by descriptor; anything else is a reference.
constexpr char kFieldKinds[] = {'Z', 'B', 'C', 'S', 'I', 'J', 'F', 'D', 'L'};
constexpr size_t kFieldKindCount = sizeof(kFieldKinds);
int FieldKind(std::string_view descriptor) {
  for (size_t i = 0; i + 1 < kFieldKindCount; ++i)
    if (descriptor.size() == 1 && descriptor[0] == kFieldKinds[i]) return static_cast<int>(i);
  return kFieldKindCount - 1;
}
std::array<ArtField*, kFieldKindCount> placeholder_instance{};
std::array<ArtField*, kFieldKindCount> placeholder_static{};

bool AnyRetiredField() { return any_retired_field.load(std::memory_order_acquire); }
std::optional<RetiredFieldInfo> FindRetiredField(const void* field) {
  if (!AnyRetiredField()) return std::nullopt;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  const auto found = retired_fields.find(field);
  if (found == retired_fields.end()) return std::nullopt;
  return found->second;
}
// Retired or placeholder field, by ID; never dereferences the ID.
bool ContainsFieldId(jfieldID id) {
  if (!AnyRetiredField()) return false;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  if (retired_field_ids.count(id)) return true;
  if (reinterpret_cast<uintptr_t>(id) & 1) return false;
  return retired_fields.count(id) || placeholder_fields.count(id);
}
void RecordFieldId(const void* field, jfieldID id) {
  if (field_capture) (*field_capture)[id] = field;
  if (!AnyRetiredField()) return;
  {
    std::shared_lock<std::shared_mutex> lock(retired_mutex);
    if (!retired_fields.count(field) && !placeholder_fields.count(field)) return;
  }
  std::unique_lock<std::shared_mutex> lock(retired_mutex);
  retired_field_ids.insert(id);
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

using class_layout::DexOf;
using class_layout::Members;
using class_layout::Fail;
using class_layout::Pointer;
using class_layout::Reference;

// ART services, resolved by name in Prepare.
void (*throw_exception)(void*, const char*, const char*) = nullptr;
ArtField* (*find_static_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_declared_instance_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
void* (*mirror_class)(ClassRedefinitionPrefix*) = nullptr;
void* original_mapping = nullptr;
void* original_update = nullptr;
void* original_allocate = nullptr;
void* original_check_verification = nullptr;
void* original_find_class = nullptr;
void* original_find_class_with_length = nullptr;
bool superclass_change_available = false;
void* original_instances = nullptr;
void* original_visit = nullptr;
void* original_mh = nullptr;
void* original_mh_exact = nullptr;
void* original_mh_frame = nullptr;
void* original_encode = nullptr;
void* original_encode_handle = nullptr;

// Member mapping of one redefinition, planned while ART may still fail and
// applied at commit without reading DEX data or calling lookups.
struct MethodMapping {
  enum Kind { kKept, kRetired, kCopied };
  void* old_method;
  void* replacement;
  Kind kind;
};
struct RetiredFieldPlan {
  ArtField* field;
  RetiredFieldInfo info;
};
// A copy of an old class's field array without its retired fields, used as
// that class's array while ART copies and clears fields (Update), so ART never
// looks for a retired field in the new class.
struct FieldArraySwap {
  int32_t class_index;
  size_t slot;
  std::vector<uint32_t> kept;  // LengthPrefixedArray<ArtField>: length, then 4 words each.
};
struct MappingPlan {
  // The target's new class links under a new superclass that extends the old
  // one: while ART copies fields it pairs the old and new superclass chains
  // level by level, so the new class temporarily names the old superclass.
  bool superclass_changed = false;
  // For each old native method, UpdateClassStructurally copies the JNI entry
  // into the new method found by name (ti_redefine.cc, 11--17): a deleted one
  // finds nothing or an inherited method, a Java replacement keeps other data
  // there. These old methods are shown as non-native for that copy only.
  std::vector<HiddenNative> hidden_natives;
  std::vector<std::pair<ArtField*, ArtField*>> fields;
  std::vector<RetiredFieldPlan> retired_fields;
  std::vector<FieldArraySwap> field_swaps;
  std::vector<MethodMapping> methods;
  // "class: method" that a re-linked class newly links as a default conflict.
  // Not a probe failure: the transaction must be rejected.
  std::string default_conflict;
};

// Mapping operations over one source-declared class layout.
struct PlanOps {
  bool (*plan)(const RedefinitionDataIter& data, MappingPlan* plan, std::string* why);
  uint64_t* (*field_slot)(void* klass, size_t slot);
  uint32_t* (*superclass)(void* klass);
};

template <typename Class, typename Method> struct Planner : class_layout::View<Class, Method> {
  using class_layout::View<Class, Method>::FieldArrays;

  // Each old field maps to the new declaration of the same name, type and
  // storage, as in ART's mapping. A field without one was accepted for
  // retirement by the policy: it keeps its real old ArtField, and ART's copy
  // phase sees a field array without it.
  static bool PlanFields(int32_t class_index, Class* old_class, Class* new_class,
                         MappingPlan* plan, std::string* why) {
    const DexFilePrefix* dex = DexOf(old_class->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "old class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    const auto arrays = FieldArrays(*old_class);
    for (size_t slot = 0; slot < arrays.size(); ++slot) {
      auto* fields = Members<ArtField>(arrays[slot]);
      FieldArraySwap swap{class_index, slot, {0}};
      bool retires = false;
      for (uint32_t i = 0; fields && i < fields->length; ++i) {
        ArtField* field = fields->At(i);
        evolution::Member identity;
        std::string error;
        if (!evolution::ReadFieldIdentity(view, field->dex_field_index, &identity, &error))
          return Fail(why, "field identity unavailable: " + error);
        const bool is_static = field->access_flags & evolution::kAccStatic;
        ArtField* replacement = (is_static ? find_static_field : find_declared_instance_field)(
            new_class, identity.name, identity.descriptor);
        if (replacement) {
          plan->fields.emplace_back(field, replacement);
          const auto* words = reinterpret_cast<const uint32_t*>(field);
          swap.kept.insert(swap.kept.end(), words, words + sizeof(ArtField) / sizeof(uint32_t));
          ++swap.kept[0];
          continue;
        }
        if (field->declaring_class != Reference(old_class))
          return Fail(why, "unexpected field without replacement: " + identity.name);
        if (!field_retirement_available) return Fail(why, "field retirement unavailable");
        plan->fields.emplace_back(field, field);
        plan->retired_fields.push_back({field, {FieldKind(identity.descriptor), is_static}});
        retires = true;
      }
      if (retires) plan->field_swaps.push_back(std::move(swap));
    }
    return true;
  }

  // Map array members from their real declaring class's DEX. ART's
  // HasSameNameAndSignature/GetCanonicalMethod call GetObsoleteDexCache,
  // which assumes an obsolete method lives in its declaring class's array.
  // A copied method lives in the implementation class's array instead; after
  // redefining its interface that assumption no longer holds.
  static bool DexMethodIdentity(Method* method, evolution::Member* identity, std::string* why) {
    auto* declaring = Pointer<Class>(method->declaring_class);
    const auto* dex = declaring ? DexOf(declaring->dex_cache) : nullptr;
    if (!dex || !dex->begin) return Fail(why, "mapped method has no declaring DEX");
    return evolution::ReadMethodIdentity(DexViewOf(*dex), method->dex_method_index, identity, why);
  }

  // Shared by every structural transaction/layout, including ordinary classes.
  // Identity is the declaring DEX's name + prototype, not a runtime canonical
  // lookup or a raw method index (indices are local to different DEX files).
  static bool PlanMethods(Class* old_class, Class* new_class, MappingPlan* plan, std::string* why) {
    auto* old_methods = Members<Method>(old_class->methods);
    auto* new_methods = Members<Method>(new_class->methods);
    std::vector<evolution::Member> identities(new_methods ? new_methods->length : 0);
    for (uint32_t j = 0; new_methods && j < new_methods->length; ++j)
      if (!DexMethodIdentity(new_methods->At(j), &identities[j], why)) return false;
    std::set<std::pair<std::string, std::string>> old_conflicts;
    for (uint32_t i = 0; old_methods && i < old_methods->length; ++i) {
      Method* old_method = old_methods->At(i);
      evolution::Member identity;
      if (!DexMethodIdentity(old_method, &identity, why)) return false;
      if (IsDefaultConflicting(*old_method)) old_conflicts.emplace(identity.name, identity.descriptor);
      const bool declared = old_method->declaring_class == Reference(old_class);
      void* replacement = nullptr;
      for (uint32_t j = 0; new_methods && j < new_methods->length; ++j) {
        Method* candidate = new_methods->At(j);
        // Removing an override may expose an inherited/default copied member
        // with the same identity. That does not preserve the old declaration.
        if (declared && candidate->declaring_class != Reference(new_class)) continue;
        if (identity.name == identities[j].name && identity.descriptor == identities[j].descriptor) {
          replacement = candidate;
          break;
        }
      }
      constexpr uint32_t kNative = evolution::kAccNative;
      if ((old_method->access_flags.load() & kNative) &&
          (!replacement || !(static_cast<Method*>(replacement)->access_flags.load() & kNative)))
        plan->hidden_natives.push_back({&old_method->access_flags, &old_method->quick_entry});
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
        auto* declaring = Pointer<Class>(old_method->declaring_class);
        auto* canonical = Members<Method>(declaring->methods);
        Method* original = nullptr;
        for (uint32_t j = 0; canonical && j < canonical->length; ++j) {
          Method* candidate = canonical->At(j);
          if (candidate->declaring_class == old_method->declaring_class &&
              candidate->dex_method_index == old_method->dex_method_index) {
            original = candidate;
            break;
          }
        }
        if (!original) return Fail(why, "copied method has no canonical declaration");
        plan->methods.push_back({old_method, original, MethodMapping::kCopied});
      }
    }
    // ClassLinker links a conflict when a class inherits one identity as the
    // default of two unrelated interfaces. A conflict the old class did not
    // have would make calls that reached a default throw from now on.
    for (uint32_t j = 0; new_methods && j < new_methods->length && plan->default_conflict.empty(); ++j)
      if (IsDefaultConflicting(*new_methods->At(j)) &&
          !old_conflicts.count({identities[j].name, identities[j].descriptor}))
        plan->default_conflict = Descriptor(*new_class) + ": " + identities[j].name + identities[j].descriptor;
    return true;
  }

  // The class's own type descriptor from its DexCache, for rejection messages.
  static std::string Descriptor(const Class& klass) {
    const DexFilePrefix* dex = DexOf(klass.dex_cache);
    std::string descriptor, error;
    if (!dex || !dex->begin || klass.dex_type_index < 0 ||
        !evolution::ReadTypeDescriptor(DexViewOf(*dex), static_cast<uint32_t>(klass.dex_type_index),
                                       &descriptor, &error))
      return "<unnamed class>";
    return descriptor;
  }

  static bool Plan(const RedefinitionDataIter& data, MappingPlan* plan, std::string* why) {
    auto* old_classes = data.Classes(true);
    auto* new_classes = data.Classes(false);
    if (!old_classes || !new_classes) return true;  // Non-structural: ART keeps the class.
    for (int32_t i = 0; i < old_classes->length; ++i) {
      auto* old_class = old_classes->template Get<Class>(i);
      auto* new_class = new_classes->template Get<Class>(i);
      if (!PlanFields(i, old_class, new_class, plan, why)) return false;
      if (!PlanMethods(old_class, new_class, plan, why)) return false;
    }
    return true;
  }

  static constexpr PlanOps Make(int) {
    return {Plan, Planner::FieldSlot, Planner::Superclass};
  }
};

// Indexed like class_layout's probes.
const auto kPlanners = class_layout::Table<Planner, PlanOps>();

// This transaction's mapping plans, by RedefinitionDataIter index.
struct Plans final : transaction::Part {
  std::map<int32_t, MappingPlan> by_index;
};
const MappingPlan* PlanOf(const transaction::Context* context, int32_t index) {
  auto* plans = transaction::PartOf<Plans>(context, &transaction::Context::mapping);
  if (!plans) return nullptr;
  const auto found = plans->by_index.find(index);
  return found == plans->by_index.end() ? nullptr : &found->second;
}


void* DecodeClass(ClassRedefinitionPrefix* self, jclass klass) {
  return class_layout::DecodeClass(mirror_class, self, klass);
}

template <typename F> void* Fn(F function) { return reinterpret_cast<void*>(function); }
template <typename F> void** Out(F* function) { return reinterpret_cast<void**>(function); }

void ThrowRetired(void* self) {
  throw_exception(self, "Ljava/lang/NoSuchMethodError;", "method retired by structural redefinition");
}

// ART links the target's new class under the old superclass it passes here
// ("We know the interfaces and supers didn't change"). For a superclass change,
// store the new superclass in that handle's slot: the slot belongs to the
// caller's handle scope, so it stays a GC root. Subclasses are allocated through
// the same redefinition with the target's new class as their superclass.
void* AllocateNewClassObject(ClassRedefinitionPrefix* self, Handle old_class, Handle super_class,
                             Handle cache, uint16_t index) noexcept {
  const transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (target && target->superclass && super_class.reference &&
      old_class.Get<void>() == mirror_class(self)) {
    if (void* superclass = DecodeClass(self, target->superclass))
      *const_cast<uint32_t*>(super_class.reference) = Reference(superclass);
  }
  using Function = void* (*)(ClassRedefinitionPrefix*, Handle, Handle, Handle, uint16_t);
  return reinterpret_cast<Function>(original_allocate)(self, old_class, super_class, cache, index);
}

// ART verifies the new DEX before the commit. Since Android 13 its verifier
// resolves the class's own descriptor through the class loader, which still
// yields the old class (old superclass), so a call to the new superclass's
// constructor fails verification. While ART verifies a redefinition with a
// superclass change, this thread's FindClass answers that descriptor with the
// new class object, which the holder keeps rooted.
struct Verifying {
  const RedefinitionDataIter* data;
  std::string_view descriptor;  // "Lpkg/Name;"
  bool interface_group;
};
thread_local const Verifying* verifying = nullptr;

bool CheckVerification(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) noexcept {
  using Function = bool (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  const transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (!target || !target->redirect_verification)
    return reinterpret_cast<Function>(original_check_verification)(self, data);
  const Verifying scope{&data, target->descriptor, interface_redefinition::IsAffected(self)};
  const Verifying* previous = std::exchange(verifying, &scope);
  const bool verified = reinterpret_cast<Function>(original_check_verification)(self, data);
  verifying = previous;
  if (const auto error = interface_redefinition::VerificationError(); error != JVMTI_ERROR_NONE) {
    self->driver->result = error;
    return false;
  }
  return verified;
}

void* VerifiedClass(void* linker, std::string_view descriptor, Handle loader) {
  if (!verifying) return nullptr;
  if (verifying->interface_group)
    return interface_redefinition::VerificationClass(linker, descriptor, loader);
  return descriptor == verifying->descriptor ? verifying->data->NewClassObject() : nullptr;
}
void* FindClass(void* linker, void* thread, const char* descriptor, Handle loader) noexcept {
  interface_redefinition::ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, descriptor, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, Handle);
  return reinterpret_cast<Function>(original_find_class)(linker, thread, descriptor, loader);
}
void* FindClassWithLength(void* linker, void* thread, const char* descriptor, size_t length,
                          Handle loader) noexcept {
  interface_redefinition::ObserveClassLinker(linker);
  if (void* klass = descriptor ? VerifiedClass(linker, {descriptor, length}, loader) : nullptr) return klass;
  using Function = void* (*)(void*, void*, const char*, size_t, Handle);
  return reinterpret_cast<Function>(original_find_class_with_length)(linker, thread, descriptor,
                                                                     length, loader);
}

// Commit: apply the plan made before ART's no-failure point, or keep ART's
// own mapping for a transaction that has none.
void CollectMappings(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                     void* method_map, void* field_map) noexcept {
  const transaction::Context* context = transaction::Current();
  const MappingPlan* plan = PlanOf(context, data.index);
  if (!plan || !abi::LibcppV1PointerMap<abi::ArtMethod>::Empty(method_map) ||
      !abi::LibcppV1PointerMap<ArtField>::Empty(field_map)) {
    if (context && context->RequiresMemberMapping())
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping unavailable at commit");
    using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&, void*, void*);
    reinterpret_cast<Function>(original_mapping)(self, data, method_map, field_map);
    return;
  }
  for (const auto& [field, replacement] : plan->fields)
    abi::LibcppV1PointerMap<ArtField>::Set(field_map, field, replacement);
  if (!plan->retired_fields.empty()) {
    {
      std::unique_lock<std::shared_mutex> lock(retired_mutex);
      for (const auto& retired : plan->retired_fields) retired_fields[retired.field] = retired.info;
    }
    any_retired_field.store(true, std::memory_order_release);
  }
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
bool CollectAndCreateNewInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) noexcept try {
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataIter*);
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  if (interface_redefinition::InjectInstanceFailure(self, data, false)) return false;
#endif
  transaction::Context* context = transaction::Current();
  if (context && context->subclass_loaded && context->subclass_loaded->load()) {
    self->driver->result = JVMTI_ERROR_INTERNAL;
    context->rejection = "a subclass of a redefined class was loaded during the transaction";
    return false;
  }
  if (!reinterpret_cast<Function>(original_instances)(self, data)) return false;
#ifdef ARTDCEVM_INTERFACE_ABORT_TESTS
  interface_redefinition::InjectInstanceFailure(self, data, true);
#endif
  if (!context) return true;
  // A class made abstract must have no instances of its own (subclass
  // instances stay valid). Reads only the common holder and Object layouts.
  // One structural owner may recreate another batch target's instances (ART
  // superclass groups or interface groups). Check every target against the
  // owner's collected instances rather than the empty dependent entry.
  for (const auto& [target, requirements] : context->targets) {
    if (!requirements.no_instances) continue;
    const auto* instances = data->OldInstances();
    const uint32_t reference = Reference(mirror_class(target));
    for (int32_t i = 0; instances && i < instances->length; ++i) {
      const auto* instance = instances->Get<Object>(i);
      if (!instance || instance->klass != reference) continue;
      self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
      context->rejection = requirements.descriptor + ": class made abstract has live instances";
      return false;
    }
  }
  if (!context->probed) return true;
  std::string why;
  if (const int selected = class_layout::VerifyReplacements(self, *data, &why); selected >= 0) {
    const PlanOps* layout = &kPlanners[selected];
    MappingPlan plan;
    if (layout->plan(*data, &plan, &why)) {
      if (const transaction::Requirements* target = context->Find(self); target && target->superclass) {
        // The allocation hook must have linked the new class under the new superclass.
        void* new_class = data->Classes(false) ? data->Classes(false)->Get<void>(0) : nullptr;
        void* superclass = DecodeClass(self, target->superclass);
        if (!new_class || !superclass || *layout->superclass(new_class) != Reference(superclass)) {
          self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
          context->rejection = target->descriptor + ": the new superclass was not applied";
          return false;
        }
        plan.superclass_changed = true;
      }
      if (!plan.default_conflict.empty()) {
        self->driver->result = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED;
        context->rejection =
            plan.default_conflict + " would become a conflict between unrelated interface defaults";
        return false;
      }
      // Later redefinitions of this batch are planned with the same layout.
      class_layout::Select(selected);
      if (!context->mapping) context->mapping = std::make_unique<Plans>();
      static_cast<Plans*>(context->mapping.get())->by_index[data->index] = std::move(plan);
      return true;
    }
    class_layout::Select(-1);
    context->mapping.reset();
  }
  Report("rejected: " + why);
  if (!context->RequiresMemberMapping()) return true;
  self->driver->result = JVMTI_ERROR_INTERNAL;
  __android_log_print(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping probe failed: %s", why.c_str());
  return false;
} catch (...) {
  self->driver->result = ArtHookExceptionError();
  return false;
}

// Puts the old classes' original field arrays back; called by the visitor
// hook, which ART invokes after its copy phase, and again after Update.
void RestoreFieldArrays() {
  for (const auto& [slot, original] : swapped_field_arrays) *slot = original;
  swapped_field_arrays.clear();
  for (const auto& [slot, original] : swapped_superclasses) *slot = original;
  swapped_superclasses.clear();
  // ART skipped these in its JNI-entry copy, but also gave them the obsolete
  // stub; a native method's entry sizes its active frames for stack walks.
  for (const auto& [method, entry] : hidden_natives) {
    method.access_flags->fetch_or(evolution::kAccNative);
    *method.quick_entry = entry;
  }
  hidden_natives.clear();
}

void Update(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) noexcept {
  struct Scope {
    bool previous = in_update;
    std::set<const void*> previous_copied = std::move(copied_resolution_changes);
    Scope() { in_update = true; copied_resolution_changes.clear(); retired_in_update.clear(); }
    ~Scope() {
      RestoreFieldArrays();
      in_update = previous;
      copied_resolution_changes = std::move(previous_copied);
    }
  } scope;
  const transaction::Context* context = transaction::Current();
  const MappingPlan* plan = PlanOf(context, data.index);
  // Every thread is suspended and moving GC is off: the old classes' native
  // field-array slots can be swapped without barriers; ART only iterates them.
  if (const PlanOps* layout = plan ? &kPlanners[class_layout::Selected()] : nullptr) {
    auto* old_classes = data.Classes(true);
    for (const auto& swap : plan->field_swaps) {
      uint64_t* slot = layout->field_slot(old_classes->Get<void>(swap.class_index), swap.slot);
      swapped_field_arrays.emplace_back(slot, *slot);
      *slot = reinterpret_cast<uintptr_t>(swap.kept.data());
    }
    // ART's CopyFields pairs target_class->GetSuperClass() with the old
    // class's superclass. The new superclass extends the old one and shares
    // its field layout, so pairing the old superclass chain with itself copies
    // every inherited field; the new superclass's own fields stay zero.
    for (const HiddenNative& method : plan->hidden_natives) {
      hidden_natives.emplace_back(method, *method.quick_entry);
      method.access_flags->fetch_and(~evolution::kAccNative);
    }
    if (plan->superclass_changed) {
      uint32_t* slot = layout->superclass(data.Classes(false)->Get<void>(0));
      swapped_superclasses.emplace_back(slot, *slot);
      *slot = *layout->superclass(data.Classes(true)->Get<void>(0));
    }
  }
  using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  reinterpret_cast<Function>(original_update)(self, data);
  RestoreFieldArrays();
  // ART's ClearFields skipped the retired statics; drop their references so
  // the retained obsolete class does not keep those objects alive.
  for (const auto& retired : plan ? plan->retired_fields : std::vector<RetiredFieldPlan>{}) {
    if (!retired.info.is_static || retired.info.kind != static_cast<int>(kFieldKindCount - 1)) continue;
    auto* holder = Pointer<uint8_t>(retired.field->declaring_class);
    std::memset(holder + retired.field->offset, 0, sizeof(uint32_t));
  }
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

// VarHandle accessors check accessModesBitMask before any field access
// (VarHandleInvokeAccessorImpl and the JIT intrinsics), so a VarHandle for a
// retired field throws UnsupportedOperationException. The offset comes from
// ART's own field table.
std::atomic<int64_t> var_handle_modes_offset{-1};
void DisableVarHandle(const void* var_handle) {
  const auto* object = static_cast<const Object*>(var_handle);
  if (!object) return;
  int64_t offset = var_handle_modes_offset.load(std::memory_order_relaxed);
  if (offset < 0) {
    const ArtField* field = find_instance_field(Pointer<void>(object->klass), "accessModesBitMask", "I");
    if (!field) {
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "VarHandle.accessModesBitMask not found");
      return;
    }
    offset = field->offset;
    var_handle_modes_offset.store(offset, std::memory_order_relaxed);
  }
  // A primitive store into an object under the commit's suspend-all.
  std::memset(const_cast<uint8_t*>(static_cast<const uint8_t*>(var_handle)) + offset, 0, sizeof(int32_t));
}

class RetirementVisitor final : public ReflectiveValueVisitor {
 public:
  explicit RetirementVisitor(ReflectiveValueVisitor* original) : original_(original) {}
  abi::ArtMethod* VisitMethod(abi::ArtMethod* method, const ReflectionSourceInfo& info) noexcept override {
    const bool retired = ContainsMethod(method);
    if (info.type == abi::kSourceDexCacheResolvedMethod &&
        (retired || copied_resolution_changes.count(method))) return nullptr;
    if (retired && info.type == abi::kSourceJniMethodId)
      RecordId(method, reinterpret_cast<const abi::JniMethodIdSourceInfo&>(info).id);
    return original_->VisitMethod(method, info);
  }
  ArtField* VisitField(ArtField* field, const ReflectionSourceInfo& info) noexcept override {
    const auto retired = FindRetiredField(field);
    if (!retired) return original_->VisitField(field, info);
    switch (info.type) {
      case abi::kSourceDexCacheResolvedField:
        return nullptr;  // Re-resolution throws NoSuchFieldError.
      case abi::kSourceJniFieldId:
        RecordFieldId(field, reinterpret_cast<const abi::JniFieldIdSourceInfo&>(info).id);
        return field;
      case abi::kSourceJavaLangReflectField:
        // ReplaceReferences would retarget the Field's declaring class to the
        // new class while its index and offset still describe the old field.
        return (retired->is_static ? placeholder_static : placeholder_instance)[retired->kind];
      case abi::kSourceJavaLangInvokeFieldVarHandle:
        DisableVarHandle(reinterpret_cast<const abi::HeapSourceInfo&>(info).source);
        return field;
      default:
        return field;  // MethodHandles are guarded; internal handles stay valid.
    }
  }
 private:
  ReflectiveValueVisitor* original_;
};
void Visit(void* runtime, ReflectiveValueVisitor* visitor) noexcept {
  using Function = void (*)(void*, ReflectiveValueVisitor*);
  if (!in_update) { reinterpret_cast<Function>(original_visit)(runtime, visitor); return; }
  // ART's copy phase is over; its visitor and fix-ups need the real arrays.
  RestoreFieldArrays();
  RetirementVisitor retirement(visitor);
  reinterpret_cast<Function>(original_visit)(runtime, &retirement);
}

// java.lang.invoke.MethodHandle.artFieldOrMethod, located through ART's own
// field table: an ArtMethod for invoke kinds, an ArtField for accessor kinds.
// Transformer handles carry neither, so the target is only looked up.
enum class HandleTarget { kLive, kRetiredMethod, kRetiredField };
std::atomic<int64_t> handle_target_offset{-1};
HandleTarget RetiredHandle(Handle handle) {
  if (!AnyRetired() && !AnyRetiredField()) return HandleTarget::kLive;
  const auto* object = handle.Get<Object>();
  if (!object) return HandleTarget::kLive;
  int64_t offset = handle_target_offset.load(std::memory_order_acquire);
  if (offset < 0) {
    const ArtField* field = find_instance_field(Pointer<void>(object->klass), "artFieldOrMethod", "J");
    if (!field) {
      __android_log_write(ANDROID_LOG_ERROR, "ART-DCEVM", "MethodHandle.artFieldOrMethod not found");
      return HandleTarget::kLive;
    }
    offset = field->offset;
    handle_target_offset.store(offset, std::memory_order_release);
  }
  uint64_t target = 0;
  std::memcpy(&target, reinterpret_cast<const uint8_t*>(object) + offset, sizeof(target));
  if (ContainsMethod(Pointer<const void>(target))) return HandleTarget::kRetiredMethod;
  if (FindRetiredField(Pointer<const void>(target))) return HandleTarget::kRetiredField;
  return HandleTarget::kLive;
}
// Throws for a retired handle target; false when the handle may run.
bool RejectHandle(void* self, Handle handle) {
  switch (RetiredHandle(handle)) {
    case HandleTarget::kRetiredMethod: ThrowRetired(self); return true;
    case HandleTarget::kRetiredField:
      throw_exception(self, "Ljava/lang/NoSuchFieldError;", "field retired by structural redefinition");
      return true;
    case HandleTarget::kLive: return false;
  }
  return false;
}
bool HandleCall(void* original, void* self, void* frame, Handle method, Handle type,
                const void* operands, uint64_t* result) {
  if (RejectHandle(self, method)) { if (result) *result = 0; return false; }
  using Function = bool (*)(void*, void*, Handle, Handle, const void*, uint64_t*);
  return reinterpret_cast<Function>(original)(self, frame, method, type, operands, result);
}
bool MethodHandleInvoke(void* self, void* frame, Handle method, Handle type,
                        const void* operands, uint64_t* result) noexcept {
  return HandleCall(original_mh, self, frame, method, type, operands, result);
}
bool MethodHandleInvokeExact(void* self, void* frame, Handle method, Handle type,
                             const void* operands, uint64_t* result) noexcept {
  return HandleCall(original_mh_exact, self, frame, method, type, operands, result);
}
void MethodHandleFrame(void* self, Handle method, Handle frame) noexcept {
  if (RejectHandle(self, method)) return;
  using Function = void (*)(void*, Handle, Handle);
  reinterpret_cast<Function>(original_mh_frame)(self, method, frame);
}
jmethodID Encode(void* manager, void* method) noexcept {
  using Function = jmethodID (*)(void*, void*);
  jmethodID id = reinterpret_cast<Function>(original_encode)(manager, method);
  if (Retired(method)) RecordId(method, id);
  return id;
}
struct ReflectiveHandle { void** reference; };
jmethodID EncodeHandle(void* manager, ReflectiveHandle method) noexcept {
  using Function = jmethodID (*)(void*, ReflectiveHandle);
  jmethodID id = reinterpret_cast<Function>(original_encode_handle)(manager, method);
  if (Retired(*method.reference)) RecordId(*method.reference, id);
  return id;
}
// JniIdManager::EncodeFieldId, in both ART spellings: record IDs encoded for
// retired or placeholder fields (FromReflectedField on a remapped Field).
void* original_encode_field = nullptr;
void* original_encode_field_handle = nullptr;
jfieldID EncodeField(void* manager, void* field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, void*)>(original_encode_field)(manager, field);
  RecordFieldId(field, id);
  return id;
}
jfieldID EncodeFieldHandle(void* manager, ReflectiveHandle field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, ReflectiveHandle)>(
      original_encode_field_handle)(manager, field);
  RecordFieldId(*field.reference, id);
  return id;
}
jfieldID EncodeFieldWithClass(void* manager, Handle klass, void* field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, Handle, void*)>(original_encode_field)(
      manager, klass, field);
  RecordFieldId(field, id);
  return id;
}
jfieldID EncodeFieldHandleWithClass(void* manager, Handle klass, ReflectiveHandle field) noexcept {
  jfieldID id = reinterpret_cast<jfieldID (*)(void*, Handle, ReflectiveHandle)>(
      original_encode_field_handle)(manager, klass, field);
  RecordFieldId(*field.reference, id);
  return id;
}
// Reflection: Method.invoke's native entry, before ART checks the receiver
// against the old declaring class. Only public JNI identifies the target.
jobject (*original_method_invoke)(JNIEnv*, jobject, jobject, jobjectArray) = nullptr;
const JNINativeInterface* jni = nullptr;  // The table in force before ours.
jclass class_type_root = nullptr;
jclass no_such_method_error = nullptr;
jclass no_such_field_error = nullptr;
jclass invocation_target_exception = nullptr;
jmethodID no_such_method_init = nullptr;
jmethodID invocation_target_init = nullptr;
constexpr char kRetiredMessage[] = "method retired by structural redefinition";

// A cached reflective Method or Constructor whose target retired: throw, as
// for a failing target, InvocationTargetException wrapping NoSuchMethodError.
bool RejectReflected(JNIEnv* env, jobject executable) {
  if (!AnyRetired()) return false;
  jmethodID id = jni->FromReflectedMethod(env, executable);
  if (!id || !ContainsId(id)) return false;
  jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  jstring message = jni->NewStringUTF(env, kRetiredMessage);
  jobject cause = message ? jni->NewObject(env, no_such_method_error, no_such_method_init, message)
                          : nullptr;
  jobject wrapped = cause ? jni->NewObject(env, invocation_target_exception,
                                            invocation_target_init, cause)
                          : nullptr;
  if (wrapped) jni->Throw(env, static_cast<jthrowable>(wrapped));
  for (jobject local : {static_cast<jobject>(message), cause, wrapped})
    if (local) jni->DeleteLocalRef(env, local);
  return true;
}
jobject MethodInvoke(JNIEnv* env, jobject method, jobject receiver, jobjectArray args) noexcept {
  return RejectReflected(env, method) ? nullptr
                                      : original_method_invoke(env, method, receiver, args);
}
// A retired constructor would run its old code on an object of the new class.
jobject (*original_constructor_new_instance)(JNIEnv*, jobject, jobjectArray) = nullptr;
jobject ConstructorNewInstance(JNIEnv* env, jobject constructor, jobjectArray args) noexcept {
  return RejectReflected(env, constructor) ? nullptr
                                           : original_constructor_new_instance(env, constructor, args);
}

// JNI field access with a retired or placeholder field ID.
bool RejectJniField(JNIEnv* env, jfieldID id) {
  if (!ContainsFieldId(id) || jni->ExceptionCheck(env)) return false;
  jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
  jni->ThrowNew(env, no_such_field_error, "field retired by structural redefinition");
  return true;
}
#define ARTDCEVM_JNI_FIELDS(T, Name)                                                              \
  T Get##Name##Field(JNIEnv* env, jobject object, jfieldID id) {                                 \
    return RejectJniField(env, id) ? T() : jni->Get##Name##Field(env, object, id);               \
  }                                                                                              \
  void Set##Name##Field(JNIEnv* env, jobject object, jfieldID id, T value) {                     \
    if (!RejectJniField(env, id)) jni->Set##Name##Field(env, object, id, value);                 \
  }                                                                                              \
  T GetStatic##Name##Field(JNIEnv* env, jclass klass, jfieldID id) {                             \
    return RejectJniField(env, id) ? T() : jni->GetStatic##Name##Field(env, klass, id);          \
  }                                                                                              \
  void SetStatic##Name##Field(JNIEnv* env, jclass klass, jfieldID id, T value) {                 \
    if (!RejectJniField(env, id)) jni->SetStatic##Name##Field(env, klass, id, value);            \
  }
ARTDCEVM_JNI_FIELDS(jobject, Object)
ARTDCEVM_JNI_FIELDS(jboolean, Boolean)
ARTDCEVM_JNI_FIELDS(jbyte, Byte)
ARTDCEVM_JNI_FIELDS(jchar, Char)
ARTDCEVM_JNI_FIELDS(jshort, Short)
ARTDCEVM_JNI_FIELDS(jint, Int)
ARTDCEVM_JNI_FIELDS(jlong, Long)
ARTDCEVM_JNI_FIELDS(jfloat, Float)
ARTDCEVM_JNI_FIELDS(jdouble, Double)
#undef ARTDCEVM_JNI_FIELDS
jobject ToReflectedField(JNIEnv* env, jclass klass, jfieldID id, jboolean is_static) {
  return RejectJniField(env, id) ? nullptr : jni->ToReflectedField(env, klass, id, is_static);
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

jobject NewObject(JNIEnv* env, jclass klass, jmethodID id, ...) {
  if (RejectJni(env, id)) return nullptr;
  va_list args;
  va_start(args, id);
  jobject result = jni->NewObjectV(env, klass, id, args);
  va_end(args);
  return result;
}
jobject NewObjectV(JNIEnv* env, jclass klass, jmethodID id, va_list args) {
  return RejectJni(env, id) ? nullptr : jni->NewObjectV(env, klass, id, args);
}
jobject NewObjectA(JNIEnv* env, jclass klass, jmethodID id, const jvalue* args) {
  return RejectJni(env, id) ? nullptr : jni->NewObjectA(env, klass, id, args);
}

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
  class_type_root = global_class("java/lang/Class");
  no_such_method_error = global_class("java/lang/NoSuchMethodError");
  invocation_target_exception = global_class("java/lang/reflect/InvocationTargetException");
  no_such_field_error = global_class("java/lang/NoSuchFieldError");
  if (!class_type_root || !no_such_method_error || !invocation_target_exception || !no_such_field_error)
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
#define ARTDCEVM_JNI_FIELD_ENTRIES(Name)                                                          \
  guarded_jni.Get##Name##Field = Get##Name##Field;                                               \
  guarded_jni.Set##Name##Field = Set##Name##Field;                                               \
  guarded_jni.GetStatic##Name##Field = GetStatic##Name##Field;                                   \
  guarded_jni.SetStatic##Name##Field = SetStatic##Name##Field;
  ARTDCEVM_JNI_FIELD_ENTRIES(Object)
  ARTDCEVM_JNI_FIELD_ENTRIES(Boolean)
  ARTDCEVM_JNI_FIELD_ENTRIES(Byte)
  ARTDCEVM_JNI_FIELD_ENTRIES(Char)
  ARTDCEVM_JNI_FIELD_ENTRIES(Short)
  ARTDCEVM_JNI_FIELD_ENTRIES(Int)
  ARTDCEVM_JNI_FIELD_ENTRIES(Long)
  ARTDCEVM_JNI_FIELD_ENTRIES(Float)
  ARTDCEVM_JNI_FIELD_ENTRIES(Double)
#undef ARTDCEVM_JNI_FIELD_ENTRIES
  guarded_jni.ToReflectedField = ToReflectedField;
  guarded_jni.NewObject = NewObject;
  guarded_jni.NewObjectV = NewObjectV;
  guarded_jni.NewObjectA = NewObjectA;
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
      {kAllocateNewClassObject, Fn(AllocateNewClassObject), &original_allocate},
      {kCheckVerification, Fn(CheckVerification), &original_check_verification},
      {kVisitReflectiveTargets, Fn(Visit), &original_visit},
      {kMethodHandleInvoke, Fn(MethodHandleInvoke), &original_mh},
      {kMethodHandleInvokeExact, Fn(MethodHandleInvokeExact), &original_mh_exact},
      {kEncodeMethodId, Fn(Encode), &original_encode},
      {kEncodeMethodIdHandle, Fn(EncodeHandle), &original_encode_handle},
      {kThrowNewException, nullptr, Out(&throw_exception)},
      {kFindDeclaredStaticField, nullptr, Out(&find_static_field)},
      {kFindDeclaredInstanceField, nullptr, Out(&find_declared_instance_field)},
      {kFindInstanceField, nullptr, Out(&find_instance_field)},
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
  void* new_instance = nullptr;
  const char* new_instance_name = nullptr;
  for (const auto& symbol : kConstructorNewInstanceNative) {
    if ((new_instance = symbols::Find(symbol))) { new_instance_name = symbol.name; break; }
  }
  if (!new_instance) {
    *note = "method retirement unavailable: missing Constructor.newInstance0 native entry";
    Report("unavailable");
    return;
  }
  resolved.push_back({new_instance, Fn(ConstructorNewInstance),
                      Out(&original_constructor_new_instance), new_instance_name});
  // Field IDs: one of ART's two EncodeFieldId pairs. Without either, field
  // retirement stays off and methods still retire.
  struct EncodePair {
    symbols::NamedSymbol plain, handle;
    void* plain_replacement;
    void* handle_replacement;
  };
  const EncodePair encode_pairs[] = {
      {kEncodeFieldId, kEncodeFieldIdHandle, Fn(EncodeField), Fn(EncodeFieldHandle)},
      {kEncodeFieldIdWithClass, kEncodeFieldIdHandleWithClass, Fn(EncodeFieldWithClass),
       Fn(EncodeFieldHandleWithClass)}};
  field_ids_hooked = false;
  for (const auto& pair : encode_pairs) {
    void* plain = symbols::Find(pair.plain);
    void* handle = symbols::Find(pair.handle);
    if (!plain || !handle) continue;
    resolved.push_back({plain, pair.plain_replacement, &original_encode_field, pair.plain.name});
    resolved.push_back({handle, pair.handle_replacement, &original_encode_field_handle,
                        pair.handle.name});
    field_ids_hooked = true;
    break;
  }
  // Superclass changes need the verifier redirect through either FindClass.
  superclass_change_available = false;
  if (void* find = symbols::Find(kFindClass)) {
    resolved.push_back({find, Fn(FindClass), &original_find_class, kFindClass.name});
    superclass_change_available = true;
  }
  if (void* find = symbols::Find(kFindClassWithLength)) {
    resolved.push_back({find, Fn(FindClassWithLength), &original_find_class_with_length,
                        kFindClassWithLength.name});
    superclass_change_available = true;
  }
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
  class_layout::Enable({find_static_field, find_declared_instance_field, mirror_class, class_type_root});
  Report("unverified");
}
bool Available() { return available; }
bool FieldRetirementAvailable() { return field_retirement_available; }
bool SuperclassChangeAvailable() { return available && superclass_change_available; }

namespace {
// ArtFields of one placeholder class, from JVMTI (which does not initialize
// the class) and the IDs its encoding recorded.
bool ResolvePlaceholders(jvmtiEnv* jvmti, jclass klass, const std::map<jfieldID, const void*>& ids,
                         std::array<ArtField*, kFieldKindCount>* out, std::string* failure) {
  jint count = 0;
  jfieldID* fields = nullptr;
  if (jvmti->GetClassFields(klass, &count, &fields) != JVMTI_ERROR_NONE)
    return Fail(failure, "GetClassFields failed for a field placeholder class");
  bool resolved = true;
  for (jint i = 0; i < count && resolved; ++i) {
    char* name = nullptr;
    char* descriptor = nullptr;
    if (jvmti->GetFieldName(klass, fields[i], &name, &descriptor, nullptr) != JVMTI_ERROR_NONE) {
      resolved = false;
      break;
    }
    const auto found = ids.find(fields[i]);
    const void* field = found != ids.end() ? found->second
        : (reinterpret_cast<uintptr_t>(fields[i]) & 1) ? nullptr : fields[i];
    auto& slot = (*out)[FieldKind(descriptor)];
    resolved = field && !slot;
    slot = static_cast<ArtField*>(const_cast<void*>(field));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(descriptor));
  }
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
  for (ArtField* field : *out) resolved &= field != nullptr;
  return resolved ? true : Fail(failure, "field placeholders unresolved");
}
}  // namespace

void ResolveFieldPlaceholders(jvmtiEnv* jvmti, const FieldPlaceholders& placeholders,
                              std::string* note) {
  field_retirement_available = false;
  if (!available || !field_ids_hooked) {
    *note = "field retirement unavailable: no JniIdManager::EncodeFieldId pair";
    return;
  }
  if (!placeholders.jni || !placeholders.instance_fields || !placeholders.static_fields) {
    *note = "field retirement unavailable: placeholder classes missing";
    return;
  }
  std::map<jfieldID, const void*> ids;
  field_capture = &ids;
  std::string failure;
  const bool resolved =
      ResolvePlaceholders(jvmti, placeholders.instance_fields, ids, &placeholder_instance, &failure) &&
      ResolvePlaceholders(jvmti, placeholders.static_fields, ids, &placeholder_static, &failure);
  field_capture = nullptr;
  if (!resolved) {
    *note = "field retirement unavailable: " + failure;
    return;
  }
  {
    std::unique_lock<std::shared_mutex> lock(retired_mutex);
    for (const auto* table : {&placeholder_instance, &placeholder_static})
      for (ArtField* field : *table) placeholder_fields.insert(field);
    for (const auto& [id, field] : ids) retired_field_ids.insert(id);
  }
  // Fail RetiredStaticField's initializer once, so every reflective access to
  // a retired static field reports the same NoClassDefFoundError.
  JNIEnv* env = placeholders.jni;
  if (!env->GetStaticFieldID(placeholders.static_fields, "retiredInt", "I") && env->ExceptionCheck())
    env->ExceptionClear();
  field_retirement_available = true;
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
uint64_t JniRetirementGuardHits() {
  return jni_retirement_guards.load(std::memory_order_relaxed);
}
}  // namespace artdcevm::method_retirement
