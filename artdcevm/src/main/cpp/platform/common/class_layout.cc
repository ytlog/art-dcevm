#include "platform/common/runtime_thread.h"
#include "platform/common/class_layout.h"

#include <android/api-level.h>
#include "platform/abi/arm64/release_order.h"
#include <mutex>
#include <optional>
#include <set>
#include <tuple>

#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/field_array_probe.h"
#include "platform/common/invoke_kind.h"
#include "evolution/modifiers.h"
#include "platform/common/transaction_context.h"

namespace artdcevm::class_layout {
namespace {
namespace abi = artdcevm::abi::arm64;
using abi::ArtField;
using abi::ClassRedefinitionPrefix;
using abi::DexFilePrefix;
using abi::RedefinitionDataIter;
using abi::kAccJavaFlagsMask;
using abi::kAccObsoleteMethod;

Services art{};
bool enabled = false;
std::mutex state_mutex;
std::string probe_state = "unavailable";

// Probes over one source-declared class layout.
struct ProbeOps {
  int android;
  void (*observe)(const VerifiedThread&, void*, void*);
  bool (*verify_target)(const VerifiedThread&, void* klass, void* class_type, const evolution::ClassSchema& live, std::string* why);
  bool (*verify_iter)(const VerifiedThread&, ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                      std::string* why);
  bool (*has_interpreted)(void* klass);
  void (*keep_interpreted)(void* klass);
  uint32_t (*access)(const void* klass);
  uint32_t (*superclass)(const void* klass);
  void (*origin)(const void* klass, uint32_t* dex_cache, uint32_t* class_loader, int32_t* class_def, int32_t* type);
};

template <typename Class, typename Method> struct Probe : View<Class, Method> {
  using View<Class, Method>::FieldArrays;

  // The class flags locate access_flags_, which follows the native member
  // arrays, so a wrong array layout fails here before any array is read. The
  // expected flags come from the class's current ClassDef in its own DEX
  // (dex_cache_ precedes the arrays in every declared layout).
  static bool VerifyAccess(const VerifiedThread& thread, const Class& klass, const std::string& descriptor, std::string* why) {
    constexpr uint32_t kValidClass = evolution::kAccPublic | evolution::kAccFinal |
        evolution::kAccSuper | evolution::kAccAbstract | evolution::kAccSynthetic | evolution::kAccEnum;
    constexpr uint32_t kValidInterface = evolution::kAccPublic | evolution::kAccInterface |
        evolution::kAccAbstract | evolution::kAccSynthetic | evolution::kAccAnnotation;
    const DexFilePrefix* dex = DexOf(thread, klass.dex_cache);
    if (!dex || !dex->begin) return Fail(why, "class has no DexFile");
    uint32_t class_def_access = 0;
    std::string error;
    if (!evolution::ReadClassAccess(DexViewOf(*dex), descriptor, &class_def_access, &error))
      return Fail(why, "class definition unavailable: " + error);
    const uint32_t expected = class_def_access &
        ((class_def_access & evolution::kAccInterface) ? kValidInterface : kValidClass);
    if ((klass.access_flags & kAccJavaFlagsMask) != expected)
      return Fail(why, "class access flags differ");
    return true;
  }

  // ART's Class.java field table confirms each slot before reading any array,
  // including the shared nonnull empty arrays used by newer ART modules.
  // Named target fields must then lie in arrays containing exactly the JVMTI fields.
  static bool VerifyFields(const VerifiedThread& thread, Class* klass, void* class_type,
                           const evolution::ClassSchema& live, std::string* why) {
    const auto arrays = FieldArrays(*klass);
    const auto slots = FieldArraySlots(*klass);
    size_t count = 0;
    for (size_t i = 0; i < arrays.size(); ++i) {
      uint32_t length = 0;
      const auto offset = static_cast<uint32_t>(
          reinterpret_cast<uintptr_t>(slots[i]) - reinterpret_cast<uintptr_t>(klass));
      if (!ProbeFieldArray(class_type, FieldArrayName(*klass, i), offset, arrays[i],
                           art.find_declared_instance_field, &length))
        return Fail(why, "field array slot differs: " + std::string(FieldArrayName(*klass, i)));
      count += length;
    }
    if (count != live.fields.size()) return Fail(why, "field count differs");
    std::vector<std::pair<ArtField*, const evolution::Member*>> found;
    for (const auto& member : live.fields) {
      ArtField* field = (member.IsStatic() ? art.find_static_field : art.find_declared_instance_field)(
          klass, member.name, member.descriptor);
      if (!field) return Fail(why, "ART did not find field " + member.name);
      bool placed = false;
      for (size_t i = 0; i < arrays.size() && !placed; ++i) {
        const uintptr_t first = static_cast<uintptr_t>(arrays[i]) + sizeof(uint32_t);
        const uintptr_t at = reinterpret_cast<uintptr_t>(field);
        placed = arrays[i] && at >= first && at - first < size_t{UINT16_MAX} * sizeof(ArtField) &&
            (at - first) % sizeof(ArtField) == 0;
      }
      if (!placed) return Fail(why, "field " + member.name + " is outside the declared arrays");
      found.emplace_back(field, &member);
    }
    const DexFilePrefix* dex = DexOf(thread, klass->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    for (const auto& [field, member] : found) {
      bool inside = false;
      for (uint64_t array : arrays) inside |= array && Members<ArtField>(array)->Contains(field);
      evolution::Member identity;
      std::string error;
      if (!inside || Heap<Class>(thread, field->declaring_class) != klass ||
          (field->access_flags & kAccJavaFlagsMask) != member->access ||
          !evolution::ReadFieldIdentity(view, field->dex_field_index, &identity, &error) ||
          identity.name != member->name || identity.descriptor != member->descriptor)
        return Fail(why, "field declaration differs: " + member->name);
    }
    return true;
  }

  static bool VerifyMethods(const VerifiedThread& thread, Class* klass, const evolution::ClassSchema& live, std::string* why) {
    std::set<std::tuple<std::string, std::string, uint32_t>> expected;
    for (const auto& member : live.methods)
      expected.emplace(member.name, member.descriptor, member.access);
    auto* methods = Members<Method>(klass->methods);
    if (!methods) return expected.empty() ? true : Fail(why, "class has no methods array");
    const DexFilePrefix* dex = DexOf(thread, klass->dex_cache);
    if (!dex || !dex->begin) return Fail(why, "class has no DexFile");
    const evolution::DexView view = DexViewOf(*dex);
    size_t declared = 0;
    for (uint32_t i = 0; i < methods->length; ++i) {
      Method* method = methods->At(i);
      if (Heap<Class>(thread, method->declaring_class) != klass) continue;
      const uint32_t flags = method->access_flags.load(std::memory_order_relaxed);
      evolution::Member identity;
      std::string error;
      if ((flags & kAccObsoleteMethod) ||
          !evolution::ReadMethodIdentity(view, method->dex_method_index, &identity, &error) ||
          !expected.count({identity.name, identity.descriptor,
                           evolution::MethodModifiers(flags)}))
        return Fail(why, "method declaration differs");
      ++declared;
    }
    if (declared != expected.size()) return Fail(why, "method count differs");
    return true;
  }

  static bool VerifyTarget(const VerifiedThread& thread, void* klass, void* class_type,
                           const evolution::ClassSchema& live, std::string* why) {
    auto* declared = static_cast<Class*>(klass);
    return declared && VerifyAccess(thread, *declared, live.descriptor, why) &&
        VerifyFields(thread, declared, class_type, live, why) && VerifyMethods(thread, declared, live, why);
  }

  // After ART allocated the replacement classes: the holder's class slots
  // must name the target and a class loaded from the incoming DEX.
  static bool VerifyIter(const VerifiedThread& thread, ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                         std::string* why) {
    const auto* old_classes = data.Classes(true);
    const auto* new_classes = data.Classes(false);
    // A non-structural redefinition keeps its class and fills neither slot.
    if (!old_classes && !new_classes) return true;
    if (!old_classes || !new_classes || old_classes->length < 1 ||
        old_classes->length != new_classes->length)
      return Fail(why, "redefinition class slots are malformed");
    if (Element<void>(thread, old_classes, 0) != art.mirror_class(self))
      return Fail(why, "redefinition slot does not hold the target class");
    for (int32_t i = 0; i < new_classes->length; ++i) {
      auto* old_class = Element<Class>(thread, old_classes, i);
      auto* new_class = Element<Class>(thread, new_classes, i);
      if (!old_class || !new_class) return Fail(why, "redefinition class slot is empty");
      if (i == 0 && DexOf(thread, new_class->dex_cache) != self->dex_file)
        return Fail(why, "replacement class does not use the incoming DEX");
      auto* methods = Members<Method>(new_class->methods);
      for (uint32_t j = 0; methods && j < methods->length; ++j)
        if (!methods->At(j)->declaring_class) return Fail(why, "replacement method is unlinked");
    }
    return true;
  }

  static bool HasInterpreted(void* klass) {
    auto* methods = Members<Method>(static_cast<Class*>(klass)->methods);
    for (uint32_t i = 0; methods && i < methods->length; ++i) {
      Method* method = methods->At(i);
      const uint32_t flags = method->access_flags.load();
      if (method->declaring_class == Reference(klass) &&
          (flags & (abi::kAccCompileDontBother | Method::kPreCompiled | 0x80000000 | 0x0100 | 0x0400)) ==
              abi::kAccCompileDontBother) return true;
    }
    return false;
  }
  static void KeepInterpreted(void* klass) {
    auto* methods = Members<Method>(static_cast<Class*>(klass)->methods);
    for (uint32_t i = 0; methods && i < methods->length; ++i) {
      Method* method = methods->At(i);
      if (method->declaring_class == Reference(klass))
        invoke_kind::KeepInterpreted(method, Method::kPreCompiled);
    }
  }
  static uint32_t Access(const void* klass) {
    return static_cast<const Class*>(klass)->access_flags & kAccJavaFlagsMask;
  }

  static uint32_t Superclass(const void* klass) { return static_cast<const Class*>(klass)->super_class; }
  static void Origin(const void* klass, uint32_t* dex_cache, uint32_t* class_loader, int32_t* class_def, int32_t* type) {
    const auto* declared = static_cast<const Class*>(klass);
    *dex_cache = declared->dex_cache;
    *class_loader = declared->class_loader;
    *class_def = declared->dex_class_def_index;
    *type = declared->dex_type_index;
  }
  static void Observe(const VerifiedThread& thread, void* klass, void* class_type) {
    const auto* ext_field = art.find_instance_field(class_type, "extData", "Ldalvik/system/ClassExt;");
    if (!ext_field || ext_field->offset != offsetof(Class, ext_data)) return;
    auto* ext = Heap<abi::Object>(thread, static_cast<Class*>(klass)->ext_data);
    if (!ext) return;
    void* type = Heap<void>(thread, ext->klass);
    const auto* methods = art.find_instance_field(type, "obsoleteMethods", "Ljava/lang/Object;");
    if (methods)
      invoke_kind::ObserveClassLayout(offsetof(Class, access_flags), offsetof(Class, dex_cache),
                                     offsetof(Class, ext_data), methods->offset, runtime_thread::Layout(thread).is_gc_marking,
                                     offsetof(Method, dex_method_index), offsetof(Class, iftable), abi::FrameReferenceFor<Class>());
  }
  static constexpr ProbeOps Make(int android) {
    return {android, Observe, VerifyTarget, VerifyIter, HasInterpreted, KeepInterpreted, Access, Superclass, Origin};
  }
};

const auto kProbes = Table<Probe, ProbeOps>();

std::vector<size_t> CandidateOrder() {
  std::vector<size_t> order;
  for (int release : abi::CandidateReleases(android_get_device_api_level()))
    order.push_back(static_cast<size_t>(release - 11));
  return order;
}

// The first layout in candidate order that every probe of this transaction accepted.
int Selected(const transaction::Context* context) {
  if (!context || !context->probed) return -1;
  for (size_t i : CandidateOrder())
    if (context->layouts & (LayoutMask{1} << i)) return static_cast<int>(i);
  return -1;
}
}  // namespace

void Prepare(const Services& services) {
  art = services;
  enabled = art.find_static_field && art.find_declared_instance_field && art.mirror_class && art.class_type;
}

std::vector<int> CandidateReleases(bool verified_only) {
  const transaction::Context* context = transaction::Current();
  std::vector<int> releases;
  for (size_t i : CandidateOrder())
    if (!verified_only || (context && context->probed && (context->layouts & (LayoutMask{1} << i))))
      releases.push_back(kProbes[i].android);
  return releases;
}

bool VerifyTarget(const std::optional<VerifiedThread>& thread, ClassRedefinitionPrefix* self, const evolution::ClassSchema& live) {
  transaction::Context* context = transaction::Current();
  if (!enabled || !context) return false;
  if (!thread) {
    context->probed = true;
    context->layouts = 0;
    Report(std::string("rejected: ") + runtime_thread::Rejection());
    return false;
  }
  void* klass = art.mirror_class(self);
  void* class_type = DecodeClass(art.mirror_class, self, art.class_type);
  if (!class_type) return false;
  LayoutMask accepted = 0;
  std::string why;
  for (size_t i : CandidateOrder()) {
    std::string reason;
    if (kProbes[i].verify_target(*thread, klass, class_type, live, &reason))
      accepted |= LayoutMask{1} << i;
    else if (why.empty())  // The platform's own release is probed first.
      why = "android" + std::to_string(kProbes[i].android) + ": " + reason;
  }
  if (why.empty()) why = "no candidate layout for this platform";
  context->probed = true;
  context->layouts &= accepted;
  const int layout = Selected(context);
  if (layout >= 0) {
    kProbes[layout].observe(*thread, klass, class_type);
  }
  Report(layout >= 0 ? "verified: android" + std::to_string(kProbes[layout].android) + " class layout"
                     : "rejected: " + why);
  return layout >= 0;
}

bool HasInterpretedMethods(const VerifiedThread&, ClassRedefinitionPrefix* self, jclass klass) {
  return kProbes[Selected()].has_interpreted(DecodeClass(art.mirror_class, self, klass));
}
void KeepInterpreted(const VerifiedThread&, ClassRedefinitionPrefix* self, jclass klass) {
  kProbes[Selected()].keep_interpreted(DecodeClass(art.mirror_class, self, klass));
}

bool ClassAccess(const VerifiedThread&, ClassRedefinitionPrefix* self, jclass klass, uint32_t* access) {
  const transaction::Context* context = transaction::Current();
  if (!enabled || !context || !context->probed) return false;
  const void* mirror = DecodeClass(art.mirror_class, self, klass);
  if (!mirror) return false;
  std::optional<uint32_t> agreed;
  for (size_t i = 0; i < kLayoutCount; ++i) {
    if (!(context->layouts & (LayoutMask{1} << i))) continue;
    const uint32_t flags = kProbes[i].access(mirror);
    if (agreed && *agreed != flags) return false;
    agreed = flags;
  }
  if (!agreed) return false;
  *access = *agreed;
  return true;
}

bool ClassOrigin(const VerifiedThread& thread, ClassRedefinitionPrefix* self, jclass klass,
                 const DexFilePrefix** dex, int32_t* class_def, bool* boot, std::string* descriptor) {
  const transaction::Context* context = transaction::Current();
  if (!enabled || !context || !context->probed) return false;
  const void* mirror = DecodeClass(art.mirror_class, self, klass);
  if (!mirror) return false;
  std::optional<std::tuple<uint32_t, uint32_t, int32_t, int32_t>> agreed;
  for (size_t i = 0; i < kLayoutCount; ++i) {
    if (!(context->layouts & (LayoutMask{1} << i))) continue;
    std::tuple<uint32_t, uint32_t, int32_t, int32_t> origin;
    kProbes[i].origin(mirror, &std::get<0>(origin), &std::get<1>(origin), &std::get<2>(origin), &std::get<3>(origin));
    if (agreed && *agreed != origin) return false;
    agreed = origin;
  }
  if (!agreed) return false;
  *dex = DexOf(thread, std::get<0>(*agreed));
  *boot = std::get<1>(*agreed) == 0;
  *class_def = std::get<2>(*agreed);
  std::string error;
  if (!*dex || !(*dex)->begin || std::get<3>(*agreed) < 0 ||
      !evolution::ReadTypeDescriptor(DexViewOf(**dex), static_cast<uint32_t>(std::get<3>(*agreed)), descriptor, &error))
    return false;
  return true;
}

int VerifyReplacements(const VerifiedThread& thread, ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                       std::string* why) {
  transaction::Context* context = transaction::Current();
  for (size_t i = 0; context && i < kLayoutCount; ++i) {
    const LayoutMask bit = LayoutMask{1} << i;
    if ((context->layouts & bit) && !kProbes[i].verify_iter(thread, self, data, why))
      context->layouts &= ~bit;
  }
  return Selected(context);
}

bool SuperclassMatches(const VerifiedThread& thread, ClassRedefinitionPrefix* self,
                       const RedefinitionDataIter& data, jclass superclass, int selected) {
  // A batched child can be owned by an ancestor/interface group's arrays.
  // Its own replacement always lives in slot 8, even without owner arrays.
  void* replacement = Heap<void>(thread, data.Reference(RedefinitionDataIter::kSlotNewClassObject));
  void* expected = DecodeClass(art.mirror_class, self, superclass);
  if (void* rebuilt = expected ? Rebuilt(thread, data.holder, expected) : nullptr) expected = rebuilt;
  return replacement && expected && kProbes[selected].superclass(replacement) == Reference(expected);
}

namespace {
int32_t HolderEntries(const abi::RedefinitionDataHolderPrefix* holder) {
  const auto* array = holder && holder->array.reference ? holder->array.Get<abi::ObjectArray>() : nullptr;
  return array ? array->length / RedefinitionDataIter::kNumSlots : 0;
}
}  // namespace

void* Rebuilt(const VerifiedThread& thread, const abi::RedefinitionDataHolderPrefix* holder, const void* old,
              const std::vector<int32_t>& entries) {
  const int32_t count = HolderEntries(holder);
  const auto search = [&](int32_t index) -> void* {
    if (index < 0 || index >= count) return nullptr;
    const RedefinitionDataIter data{index, const_cast<abi::RedefinitionDataHolderPrefix*>(holder)};
    const auto* olds = Heap<abi::ObjectArray>(thread, data.Reference(RedefinitionDataIter::kSlotOldClasses));
    const auto* news = Heap<abi::ObjectArray>(thread, data.Reference(RedefinitionDataIter::kSlotNewClasses));
    for (int32_t i = 0; olds && news && i < olds->length && i < news->length; ++i)
      if (Element<void>(thread, olds, i) == old) return Element<void>(thread, news, i);
    return nullptr;
  };
  if (!entries.empty()) {
    for (int32_t index : entries)
      if (void* found = search(index)) return found;
    return nullptr;
  }
  for (int32_t index = 0; index < count; ++index)
    if (void* found = search(index)) return found;
  return nullptr;
}

int32_t HolderEntry(const VerifiedThread& thread, ClassRedefinitionPrefix* self,
                    const abi::RedefinitionDataHolderPrefix* holder, jclass klass) {
  const void* mirror = DecodeClass(art.mirror_class, self, klass);
  for (int32_t index = 0; mirror && index < HolderEntries(holder); ++index) {
    const RedefinitionDataIter data{index, const_cast<abi::RedefinitionDataHolderPrefix*>(holder)};
    if (Heap<void>(thread, data.Reference(RedefinitionDataIter::kSlotMirrorClass)) == mirror) return index;
  }
  return -1;
}

int Selected() { return Selected(transaction::Current()); }

void Select(int index) {
  if (transaction::Context* context = transaction::Current())
    context->layouts = index >= 0 ? LayoutMask{1} << index : 0;
}

void Report(std::string state) {
  std::lock_guard<std::mutex> lock(state_mutex);
  probe_state = std::move(state);
}
std::string State() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return probe_state;
}
}  // namespace artdcevm::class_layout
