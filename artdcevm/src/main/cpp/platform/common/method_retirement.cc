#include "platform/common/method_retirement.h"
#include "platform/common/interface_redefinition.h"
#include "platform/common/art_hook.h"

#include <android/log.h>
#include <jvmti.h>
#include <cstring>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "platform/abi/arm64/common/libcpp_pointer_map.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/abi/arm64/common/reflective_value_visitor.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/member_retirement.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/verification_redirect_symbols.h"

namespace artdcevm::method_retirement {
namespace {
namespace abi = artdcevm::abi::arm64;
using abi::ArtField;
using abi::ClassRedefinitionPrefix;
using abi::DexFilePrefix;
using abi::Handle;
using abi::Object;
using abi::RedefinitionDataIter;
using abi::ReflectiveValueVisitor;
using member_retirement::RetiredField;

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

using class_layout::Report;

using class_layout::DexOf;
using class_layout::Members;
using class_layout::Fail;
using class_layout::Pointer;
using class_layout::Reference;

// ART services, resolved by name in Prepare.
ArtField* (*find_static_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_declared_instance_field)(void*, std::string_view, std::string_view) = nullptr;
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

// Member mapping of one redefinition, planned while ART may still fail and
// applied at commit without reading DEX data or calling lookups.
struct MethodMapping {
  enum Kind { kKept, kRetired, kCopied };
  void* old_method;
  void* replacement;
  Kind kind;
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
  std::vector<RetiredField> retired_fields;
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
        if (!member_retirement::FieldRetirementAvailable())
          return Fail(why, "field retirement unavailable");
        plan->fields.emplace_back(field, field);
        plan->retired_fields.push_back(RetiredField::Of(field, identity.descriptor, is_static));
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
  member_retirement::RetireFields(plan->retired_fields);
  for (const auto& entry : plan->methods) {
    if (entry.kind == MethodMapping::kRetired) {
      member_retirement::RetireMethod(entry.old_method);
      retired_in_update.push_back(entry.old_method);
    }
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
  for (const auto& retired : plan ? plan->retired_fields : std::vector<RetiredField>{}) {
    if (!retired.IsStaticReference()) continue;
    auto* holder = Pointer<uint8_t>(retired.field->declaring_class);
    std::memset(holder + retired.field->offset, 0, sizeof(uint32_t));
  }
  member_retirement::ConfirmObsolete(retired_in_update);
  retired_in_update.clear();
}

void Visit(void* runtime, ReflectiveValueVisitor* visitor) noexcept {
  using Function = void (*)(void*, ReflectiveValueVisitor*);
  if (!in_update) { reinterpret_cast<Function>(original_visit)(runtime, visitor); return; }
  // ART's copy phase is over; its visitor and fix-ups need the real arrays.
  RestoreFieldArrays();
  member_retirement::Visitor retirement(visitor, copied_resolution_changes);
  reinterpret_cast<Function>(original_visit)(runtime, &retirement);
}

}  // namespace

void Prepare(JavaVM* vm, jvmtiEnv* jvmti, std::vector<Hook>* hooks, std::string* note) {
  available = false;
  if (!abi::LibcppV1PointerMap<abi::ArtMethod>::Compatible() ||
      !abi::LibcppV1PointerMap<ArtField>::Compatible()) {
    *note = "method retirement unavailable: NDK pointer-map layout differs from libc++ ABI v1";
    Report("unavailable");
    return;
  }
  const auto unavailable = [&](const std::string& failure) {
    *note = "method retirement unavailable: " + failure;
    Report("unavailable");
  };
  using namespace symbols::structural;
  using namespace symbols::verification_redirect;
  struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Required required[] = {
      {kCollectNewFieldAndMethodMappings, HookAddress(CollectMappings), &original_mapping},
      {kCollectAndCreateNewInstances, HookAddress(CollectAndCreateNewInstances), &original_instances},
      {kUpdateClassStructurally, HookAddress(Update), &original_update},
      {kAllocateNewClassObject, HookAddress(AllocateNewClassObject), &original_allocate},
      {kCheckVerification, HookAddress(CheckVerification), &original_check_verification},
      {kVisitReflectiveTargets, HookAddress(Visit), &original_visit},
      {kFindDeclaredStaticField, nullptr, HookSlot(&find_static_field)},
      {kFindDeclaredInstanceField, nullptr, HookSlot(&find_declared_instance_field)},
      {kClassRedefinitionGetMirrorClass, nullptr, HookSlot(&mirror_class)}};
  std::vector<Hook> resolved;
  for (const auto& entry : required) {
    void* address = symbols::Find(entry.symbol);
    if (!address) return unavailable(std::string("missing ") + entry.symbol.name);
    if (entry.replacement) resolved.push_back({address, entry.replacement, entry.original, entry.symbol.name});
    else *entry.original = address;
  }
  // Superclass changes need the verifier redirect through either FindClass.
  superclass_change_available = false;
  if (void* find = symbols::Find(kFindClass)) {
    resolved.push_back({find, HookAddress(FindClass), &original_find_class, kFindClass.name});
    superclass_change_available = true;
  }
  if (void* find = symbols::Find(kFindClassWithLength)) {
    resolved.push_back({find, HookAddress(FindClassWithLength), &original_find_class_with_length,
                        kFindClassWithLength.name});
    superclass_change_available = true;
  }
  // The layout probes decode java.lang.Class through this global reference.
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env)
    return unavailable("no JNIEnv for the JNI guards");
  jclass class_type = env->FindClass("java/lang/Class");
  jclass class_type_root = class_type ? static_cast<jclass>(env->NewGlobalRef(class_type)) : nullptr;
  if (class_type) env->DeleteLocalRef(class_type);
  if (!class_type_root) {
    env->ExceptionClear();
    return unavailable("exception classes unavailable");
  }
  // The guards come last: their JNI table override cannot be undone.
  std::string failure;
  if (!member_retirement::Prepare(vm, jvmti, &resolved, &failure)) return unavailable(failure);
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  available = true;
  class_layout::Enable({find_static_field, find_declared_instance_field, mirror_class, class_type_root});
  Report("unverified");
}
bool Available() { return available; }
bool SuperclassChangeAvailable() { return available && superclass_change_available; }

}  // namespace artdcevm::method_retirement
