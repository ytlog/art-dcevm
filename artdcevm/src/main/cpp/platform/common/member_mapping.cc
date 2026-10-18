#include "platform/common/runtime_thread.h"
#include "platform/common/member_mapping.h"
#include "platform/common/art_method_identity.h"
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
#include "platform/common/proxy_relink.h"
#include "platform/common/narrowed_references.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::member_mapping {
namespace {
namespace abi = artdcevm::abi::arm64;
using abi::ArtField;
using abi::ClassRedefinitionPrefix;
using abi::DexFilePrefix;
using abi::RedefinitionDataIter;
using abi::ReflectiveValueVisitor;
using member_retirement::RetiredField;

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

using class_layout::DexOf;
using class_layout::Members;
using class_layout::Fail;
using class_layout::Pointer;

// ART services, resolved by name in Prepare.
ArtField* (*find_static_field)(void*, std::string_view, std::string_view) = nullptr;
ArtField* (*find_declared_instance_field)(void*, std::string_view, std::string_view) = nullptr;
void* (*mirror_class)(ClassRedefinitionPrefix*) = nullptr;
void* original_mapping = nullptr;
void* original_update = nullptr;
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
// A retired reference field with a same-named new field of another reference
// type in an instance: a compatible old value moves to the new field.
// Static values use the separately planned JBR FieldCopier rules below.
struct RetypedField {
  int32_t class_index;
  ArtField* old_field;
  ArtField* new_field;
  bool is_static;
  jobject type;  // The new field type, a global reference owned by the batch.
};
// JBR FieldCopier checks only redefined instance classes, against the new
// signature of the class, its parents and their directly declared interfaces.
// The compatible Class roots are resolved while failure is still possible.
struct StaticFieldPlan {
  int32_t class_index;
  ArtField* old_field;
  ArtField* new_field;
  bool retyped;
  std::vector<jobject> compatible;
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
  std::vector<RetypedField> retyped;
  std::vector<StaticFieldPlan> statics;
  std::set<std::string> removed_interfaces;
  std::map<std::string, std::vector<jobject>> static_compatibility;
  std::vector<MethodMapping> methods;
};

bool Initializer(const std::string& name) { return name == "<init>" || name == "<clinit>"; }

// Mapping operations over one source-declared class layout.
struct PlanOps {
  bool (*plan)(const runtime_thread::VerifiedThread&, const RedefinitionDataIter& data, MappingPlan* plan, std::string* why);
  uint64_t* (*field_slot)(void* klass, size_t slot);
  uint32_t* (*superclass)(void* klass);
  void (*copy_retyped)(const runtime_thread::VerifiedThread&, const RedefinitionDataIter& data, const MappingPlan& plan);
  void (*clear_narrowed)(const runtime_thread::VerifiedThread&, const RedefinitionDataIter& data, const MappingPlan& plan);
};
// Named JavaVMExt::DecodeGlobal and the runtime's JavaVM; without them a
// retyped field starts from null like any retired field.
using DecodeGlobal = void* (*)(JavaVM*, jobject);
DecodeGlobal decode_global = nullptr;
JavaVM* runtime_vm = nullptr;

template <typename Class, typename Method> struct Planner : class_layout::View<Class, Method> {
  using class_layout::View<Class, Method>::FieldArrays;

  // Each old field maps to the new declaration of the same name, type and
  // storage, as in ART's mapping. A field without one was accepted for
  // retirement by the policy: it keeps its real old ArtField, and ART's copy
  // phase sees a field array without it.
  static bool PlanFields(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                         int32_t class_index, Class* old_class, Class* new_class,
                         MappingPlan* plan, std::string* why) {
    const DexFilePrefix* dex = DexOf(thread, old_class->dex_cache);
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
          if (is_static && identity.descriptor.front() == 'L' && plan->removed_interfaces.count(identity.descriptor))
            if (!PlanStatic(thread, data, class_index, field, replacement, identity.descriptor, false, plan, why)) return false;
          plan->fields.emplace_back(field, replacement);
          const auto* words = reinterpret_cast<const uint32_t*>(field);
          swap.kept.insert(swap.kept.end(), words, words + sizeof(ArtField) / sizeof(uint32_t));
          ++swap.kept[0];
          continue;
        }
        if (class_layout::Heap<Class>(thread, field->declaring_class) != old_class)
          return Fail(why, "unexpected field without replacement: " + identity.name);
        if (!member_retirement::FieldRetirementAvailable())
          return Fail(why, "field retirement unavailable");
        plan->fields.emplace_back(field, field);
        plan->retired_fields.push_back(RetiredField::Of(field, identity.descriptor, is_static));
        retires = true;
        if (is_static && identity.descriptor.front() == 'L') {
          const DexFilePrefix* new_dex = DexOf(thread, new_class->dex_cache);
          if (!new_dex) return Fail(why, "new static class has no DEX");
          for (uint64_t members : FieldArrays(*new_class)) {
            auto* candidates = Members<ArtField>(members);
            for (uint32_t j = 0; candidates && j < candidates->length; ++j) {
              ArtField* next = candidates->At(j);
              if (!(next->access_flags & evolution::kAccStatic)) continue;
              evolution::Member incoming;
              if (!evolution::ReadFieldIdentity(DexViewOf(*new_dex), next->dex_field_index, &incoming, why)) return false;
              if (incoming.name == identity.name && incoming.descriptor.front() == 'L')
                if (!PlanStatic(thread, data, class_index, field, next, incoming.descriptor, true, plan, why)) return false;
            }
          }
        }
        if (const auto* retyped = is_static ? nullptr : FindRetyped(thread, *old_class, identity.name, false);
            retyped && decode_global) {
          ArtField* next = (is_static ? find_static_field : find_declared_instance_field)(
              new_class, identity.name, retyped->descriptor);
          if (next) plan->retyped.push_back({class_index, field, next, is_static, retyped->type});
        }
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
  // Shared by every structural transaction/layout, including ordinary classes.
  // Identity is the declaring DEX's name + prototype, not a runtime canonical
  // lookup or a raw method index (indices are local to different DEX files).
  static bool PlanMethods(const runtime_thread::VerifiedThread& thread, Class* old_class, Class* new_class, MappingPlan* plan, std::string* why) {
    auto* old_methods = Members<Method>(old_class->methods);
    auto* new_methods = Members<Method>(new_class->methods);
    std::vector<evolution::Member> identities(new_methods ? new_methods->length : 0);
    for (uint32_t j = 0; new_methods && j < new_methods->length; ++j)
      if (!class_layout::MethodIdentity<Class>(thread, new_methods->At(j), &identities[j], why)) return false;
    for (uint32_t i = 0; old_methods && i < old_methods->length; ++i) {
      Method* old_method = old_methods->At(i);
      evolution::Member identity;
      if (!class_layout::MethodIdentity<Class>(thread, old_method, &identity, why)) return false;
      const bool declared = class_layout::Heap<Class>(thread, old_method->declaring_class) == old_class;
      void* replacement = nullptr;
      for (uint32_t j = 0; new_methods && j < new_methods->length; ++j) {
        Method* candidate = new_methods->At(j);
        // Removing an override may expose an inherited/default copied member
        // with the same identity. That does not preserve the old declaration.
        if (declared && class_layout::Heap<Class>(thread, candidate->declaring_class) != new_class) continue;
        if (identity.name == identities[j].name && identity.descriptor == identities[j].descriptor) {
          replacement = candidate;
          break;
        }
      }
      // An accepted invoke-kind change retires the old declaration: the new
      // method of the other kind is a new member, never its mapping target.
      if (replacement && declared && !Initializer(identity.name)) {
        const uint32_t before = old_method->access_flags.load(), after = static_cast<Method*>(replacement)->access_flags.load();
        constexpr uint32_t kStatic = evolution::kAccStatic, kPrivate = evolution::kAccPrivate;
        if (((before ^ after) & kStatic) || (!(before & (kStatic | kPrivate)) && (after & kPrivate))) replacement = nullptr;
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
        auto* declaring = class_layout::Heap<Class>(thread, old_method->declaring_class);
        auto* canonical = Members<Method>(declaring->methods);
        Method* original = nullptr;
        for (uint32_t j = 0; canonical && j < canonical->length; ++j) {
          Method* candidate = canonical->At(j);
          if (class_layout::Heap<Class>(thread, candidate->declaring_class) == declaring &&
              candidate->dex_method_index == old_method->dex_method_index) {
            original = candidate;
            break;
          }
        }
        if (!original) return Fail(why, "copied method has no canonical declaration");
        plan->methods.push_back({old_method, original, MethodMapping::kCopied});
      }
    }
    return true;
  }

  static bool Plan(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data, MappingPlan* plan, std::string* why) {
    auto* old_classes = data.Classes(true);
    auto* new_classes = data.Classes(false);
    if (!old_classes || !new_classes) return true;  // Non-structural: ART keeps the class.
    if (!RemovedInterfaces(thread, data, &plan->removed_interfaces, why)) return false;
    for (int32_t i = 0; i < old_classes->length; ++i) {
      auto* old_class = class_layout::Element<Class>(thread, old_classes, i);
      auto* new_class = class_layout::Element<Class>(thread, new_classes, i);
      if (!PlanFields(thread, data, i, old_class, new_class, plan, why)) return false;
      if (!PlanMethods(thread, old_class, new_class, plan, why)) return false;
    }
    return true;
  }

  // The submitted target's resolved new type for a retyped field, if any.
  static const transaction::RetypedType* FindRetyped(const runtime_thread::VerifiedThread& thread, const Class& old_class,
                                                     const std::string& name, bool is_static) {
    const transaction::Context* context = transaction::Current();
    if (!context) return nullptr;
    (void)thread;
    for (const auto& [target, requirements] : context->targets) {
      if (mirror_class(target) != &old_class) continue;
      for (const auto& retyped : requirements.retyped_fields)
        if (retyped.name == name && retyped.is_static == is_static) return &retyped;
    }
    return nullptr;
  }
  // Commit, every thread suspended and moving GC off, before ART copies the
  // fields: ART's CopyAndClearFields then runs its write barrier for each
  // target object, which covers these stores too.
  static void CopyRetyped(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                          const MappingPlan& plan) {
    auto* old_classes = data.Classes(true);
    auto* new_classes = data.Classes(false);
    auto* olds = data.OldInstances();
    auto* news = data.Slot(RedefinitionDataIter::kSlotNewInstanceObjects);
    CopyStatics(thread, data, plan);
    std::map<std::pair<Class*, std::string>, bool> compatible;
    for (const auto& retyped : plan.retyped) {
      auto* type = static_cast<Class*>(decode_global(runtime_vm, retyped.type));
      if (!type) continue;
      const std::string signature = class_layout::ClassDescriptor(thread, *type);
      auto* old_class = class_layout::Element<Class>(thread, old_classes, retyped.class_index);
      auto* new_class = class_layout::Element<Class>(thread, new_classes, retyped.class_index);
      const auto copy = [&](void* from, void* to) {
        const uint32_t reference = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(from) + retyped.old_field->offset);
        auto* value = static_cast<abi::Object*>(runtime_thread::Read(thread, reference));
        if (!value) return;
        auto* klass = Replacement(thread, data, class_layout::Heap<Class>(thread, value->klass));
        auto found = compatible.find({klass, signature});
        if (found == compatible.end())
          found = compatible.emplace(std::make_pair(klass, signature), InstanceCompatible(thread, data, klass, signature)).first;
        if (!found->second) return;
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(to) + retyped.new_field->offset) = class_layout::Reference(value);
      };
      if (retyped.is_static) { copy(old_class, new_class); continue; }
      for (int32_t i = 0; olds && news && i < olds->length; ++i) {
        auto* instance = class_layout::Element<abi::Object>(thread, olds, i);
        for (Class* c = class_layout::Heap<Class>(thread, instance->klass); c; c = class_layout::Heap<Class>(thread, c->super_class))
          if (c == old_class) { copy(instance, class_layout::Element<abi::Object>(thread, news, i)); break; }
      }
    }
  }
  // The interfaces `klass` declares itself: its ClassDef list, a Proxy class's
  // `interfaces` static (ClassLinker::CreateProxyClass), or an array class's IfTable.
  static bool DirectInterfaces(const runtime_thread::VerifiedThread& thread, Class* klass,
                               std::vector<std::string>* interfaces) {
    std::string why;
    if (const DexFilePrefix* dex = DexOf(thread, klass->dex_cache); dex && klass->dex_class_def_index >= 0)
      return evolution::ReadClassInterfaces(DexViewOf(*dex), klass->dex_class_def_index, interfaces, &why);
    if (ArtField* field = find_static_field(klass, "interfaces", "[Ljava/lang/Class;")) {
      auto* array = class_layout::Heap<abi::ObjectArray>(
          thread, *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(klass) + field->offset));
      for (int32_t i = 0; array && i < array->length; ++i)
        interfaces->push_back(class_layout::ClassDescriptor(thread, *class_layout::Element<Class>(thread, array, i)));
      return true;
    }
    auto* table = class_layout::Heap<abi::ObjectArray>(thread, klass->iftable);
    for (int32_t i = 0; table && i + 1 < table->length; i += 2)
      interfaces->push_back(class_layout::ClassDescriptor(thread, *class_layout::Element<Class>(thread, table, i)));
    return true;
  }
  // JBR's DcevmSharedGC::is_compatible for a retyped instance field: the
  // newest class chain of the value names the field type, as a class or as an
  // interface one of those classes declares directly. A superinterface of a
  // declared interface does not count, and names are compared, not classes.
  static bool InstanceCompatible(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                                 Class* klass, const std::string& signature) {
    for (Class* c = klass; c; c = Replacement(thread, data, class_layout::Heap<Class>(thread, c->super_class))) {
      if (class_layout::ClassDescriptor(thread, *c) == signature) return true;
      std::vector<std::string> interfaces;
      if (!DirectInterfaces(thread, c, &interfaces)) return false;
      for (const auto& direct : interfaces)
        if (direct == signature) return true;
    }
    return false;
  }
  static Class* Replacement(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data, Class* klass) {
    auto* holder = data.holder->array.Get<abi::ObjectArray>();
    for (int32_t owner = 0; holder && owner < holder->length / RedefinitionDataIter::kNumSlots; ++owner) {
      RedefinitionDataIter item{owner, data.holder};
      auto* olds = item.Classes(true);
      auto* news = item.Classes(false);
      for (int32_t i = 0; olds && news && i < olds->length; ++i)
        if (class_layout::Element<Class>(thread, olds, i) == klass)
          return class_layout::Element<Class>(thread, news, i);
    }
    return klass;
  }
  static bool RemovedInterfaces(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                                std::set<std::string>* removed, std::string*) {
    auto* holder = data.holder->array.Get<abi::ObjectArray>();
    for (int32_t owner = 0; holder && owner < holder->length / RedefinitionDataIter::kNumSlots; ++owner) {
      RedefinitionDataIter item{owner, data.holder};
      auto* olds = item.Classes(true);
      auto* news = item.Classes(false);
      for (int32_t i = 0; olds && news && i < olds->length; ++i) {
        auto* old_class = class_layout::Element<Class>(thread, olds, i);
        auto* new_class = class_layout::Element<Class>(thread, news, i);
        auto* before = class_layout::Heap<abi::ObjectArray>(thread, old_class->iftable);
        auto* after = class_layout::Heap<abi::ObjectArray>(thread, new_class->iftable);
        for (int32_t j = 0; before && j + 1 < before->length; j += 2) {
          Class* prior = class_layout::Element<Class>(thread, before, j);
          Class* final = Replacement(thread, data, prior);
          bool kept = false;
          for (int32_t k = 0; after && k + 1 < after->length; k += 2)
            kept |= Replacement(thread, data, class_layout::Element<Class>(thread, after, k)) == final;
          if (!kept) removed->insert(class_layout::ClassDescriptor(thread, *prior));
        }
      }
    }
    return true;
  }
  static bool StaticCompatible(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                               Class* value, const std::string& signature, bool* compatible, std::string* why) {
    *compatible = false;
    for (Class* c = value; c; c = Replacement(thread, data, class_layout::Heap<Class>(thread, c->super_class))) {
      if (class_layout::ClassDescriptor(thread, *c) == signature) { *compatible = true; return true; }
      const DexFilePrefix* dex = DexOf(thread, c->dex_cache);
      std::vector<std::string> interfaces;
      if (dex) {
        if (!evolution::ReadClassInterfaces(DexViewOf(*dex), c->dex_class_def_index, &interfaces, why)) return false;
      } else {
        // Proxy classes have no DEX. Their direct interfaces were read using
        // public JVMTI while Native, before any ART callback.
        bool captured = false;
        for (const auto& type : transaction::Current()->affected_types) {
          auto* original = static_cast<Class*>(decode_global(runtime_vm, type.klass));
          if (c != original && c != Replacement(thread, data, original)) continue;
          interfaces = type.interfaces;
          captured = true;
          break;
        }
        if (!captured) return Fail(why, "static compatibility class has no interface snapshot");
      }
      for (const auto& direct : interfaces)
        if (direct == signature) { *compatible = true; return true; }
    }
    return true;
  }
  static bool PlanStatic(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                         int32_t index, ArtField* old_field, ArtField* new_field, const std::string& signature,
                         bool retyped, MappingPlan* plan, std::string* why) {
    auto found = plan->static_compatibility.find(signature);
    if (found == plan->static_compatibility.end()) {
      if (!decode_global) return Fail(why, "static compatibility root decoding unavailable");
      std::vector<jobject> compatible;
      for (const auto& type : transaction::Current()->affected_types) {
        auto* original = static_cast<Class*>(decode_global(runtime_vm, type.klass));
        if (!original) return Fail(why, "static compatibility class root unavailable");
        bool accepted = false;
        if (!StaticCompatible(thread, data, Replacement(thread, data, original), signature, &accepted, why)) return false;
        if (accepted) compatible.push_back(type.klass);
      }
      found = plan->static_compatibility.emplace(signature, std::move(compatible)).first;
    }
    plan->statics.push_back({index, old_field, new_field, retyped, found->second});
    return true;
  }
  static bool AcceptsStatic(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                            const StaticFieldPlan& field, abi::Object* value) {
    auto* klass = class_layout::Heap<Class>(thread, value->klass);
    (void)data;
    for (jobject root : field.compatible)
      if (klass == decode_global(runtime_vm, root)) return true;
    return false;
  }
  static void CopyStatics(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                          const MappingPlan& plan) {
    for (const auto& field : plan.statics) {
      if (!field.retyped) continue;
      auto* old_class = class_layout::Element<Class>(thread, data.Classes(true), field.class_index);
      auto* new_class = class_layout::Element<Class>(thread, data.Classes(false), field.class_index);
      auto* source = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(old_class) + field.old_field->offset);
      auto* value = static_cast<abi::Object*>(runtime_thread::Read(thread, *source));
      if (!value || !AcceptsStatic(thread, data, field, value)) continue;
      *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(new_class) + field.new_field->offset) = class_layout::Reference(value);
    }
  }
  // No-failure commit: only planned field offsets and rooted Class identities.
  static void ClearNarrowed(const runtime_thread::VerifiedThread& thread, const RedefinitionDataIter& data,
                            const MappingPlan& plan) {
    for (const auto& field : plan.statics) {
      if (field.retyped) continue;
      auto* target = class_layout::Element<Class>(thread, data.Classes(false), field.class_index);
      auto* slot = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(target) + field.new_field->offset);
      auto* value = static_cast<abi::Object*>(runtime_thread::Read(thread, *slot));
      if (value && !AcceptsStatic(thread, data, field, value)) *slot = 0;
    }
  }
  static constexpr PlanOps Make(int) {
    return {Plan, Planner::FieldSlot, Planner::Superclass, Planner::CopyRetyped, Planner::ClearNarrowed};
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
    if (!plan->retyped.empty() || !plan->statics.empty())
      if (const auto thread = runtime_thread::Ready()) layout->copy_retyped(*thread, data, *plan);
  }
  using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  {
    narrowed_references::UpdateScope references(self, data);
    reinterpret_cast<Function>(original_update)(self, data);
  }
  RestoreFieldArrays();
  if (plan && class_layout::Selected() >= 0)
    if (const auto thread = runtime_thread::Ready()) kPlanners[class_layout::Selected()].clear_narrowed(*thread, data, *plan);
  // ART has replaced the interface's references, proxy IfTables included.
  proxy_relink::Apply(self, data);
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

bool Prepare(const class_layout::Services& services, std::vector<Hook>* hooks, std::string* failure) {
  if (!abi::LibcppV1PointerMap<abi::ArtMethod>::Compatible() ||
      !abi::LibcppV1PointerMap<ArtField>::Compatible())
    return Fail(failure, "NDK pointer-map layout differs from libc++ ABI v1");
  find_static_field = services.find_static_field;
  find_declared_instance_field = services.find_declared_instance_field;
  runtime_vm = services.vm;
  mirror_class = services.mirror_class;
  decode_global = reinterpret_cast<DecodeGlobal>(symbols::Find(symbols::interface_redefinition::kDecodeGlobal));
  using namespace symbols::structural;
  struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Required required[] = {
      {kCollectNewFieldAndMethodMappings, HookAddress(CollectMappings), &original_mapping},
      {kUpdateClassStructurally, HookAddress(Update), &original_update},
      {kVisitReflectiveTargets, HookAddress(Visit), &original_visit}};
  std::vector<Hook> resolved;
  for (const auto& entry : required) {
    void* address = symbols::Find(entry.symbol);
    if (!address) return Fail(failure, std::string("missing ") + entry.symbol.name);
    resolved.push_back({address, entry.replacement, entry.original, entry.symbol.name});
  }
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  return true;
}

bool Plan(const runtime_thread::VerifiedThread& thread, ClassRedefinitionPrefix* self,
          const RedefinitionDataIter& data, int selected, std::string* why) {
  auto* context = transaction::Current();
  MappingPlan plan;
  if (!kPlanners[selected].plan(thread, data, &plan, why)) {
    context->mapping.reset();
    return false;
  }
  const auto* target = context->Find(self);
  plan.superclass_changed = target && target->superclass;
  if (!context->mapping) context->mapping = std::make_unique<Plans>();
  static_cast<Plans*>(context->mapping.get())->by_index[data.index] = std::move(plan);
  return true;
}
}  // namespace artdcevm::member_mapping
