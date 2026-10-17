#include "platform/common/runtime_thread.h"
#include "platform/common/interface_dispatch.h"
#include <mutex>
#include "platform/common/fault_injection.h"
#include "platform/common/art_method_identity.h"
#include "platform/common/member_retirement.h"
#include "platform/common/transaction_context.h"

namespace artdcevm::interface_dispatch {
namespace {
namespace abi = artdcevm::abi::arm64;
using runtime_thread::VerifiedThread;
using abi::ArtField;
using abi::ClassRedefinitionPrefix;
using abi::RedefinitionDataIter;
using class_layout::Members;
using class_layout::Pointer;
using class_layout::Reference;
// Batch-wide: an interface input may depend on another group's owner.
struct Obligation {
  ClassRedefinitionPrefix* interface;
  std::string name;
  std::string descriptor;
  jclass declaring;  // Null for the target's own declaration.
};
struct Pending final : transaction::Part { std::vector<Obligation> entries; };
const std::vector<Obligation>& Obligations() {
  const auto* pending = transaction::PartOf<Pending>(transaction::Current(), &transaction::Context::dispatch);
  static const std::vector<Obligation> empty;
  return pending ? pending->entries : empty;
}
Services art{};
jclass object_array_root = nullptr, pointer_array_root = nullptr;
std::mutex state_mutex;
std::string state = "unavailable";
void Report(std::string value) {
  std::lock_guard<std::mutex> lock(state_mutex);
  state = std::move(value);
}
struct Ops {
  bool (*probe)(const VerifiedThread&, ClassRedefinitionPrefix*);
  bool (*check)(const VerifiedThread&, ClassRedefinitionPrefix*, const RedefinitionDataIter&, std::string*);
  bool (*non_public)(const VerifiedThread&, ClassRedefinitionPrefix*, const std::vector<abi::Handle>&, abi::Handle,
                     int32_t, NonPublicSelection, const std::string&, bool, std::string*);
};
template <typename Class, typename Method> struct Verifier {
  static bool DispatchProbe(const VerifiedThread&, ClassRedefinitionPrefix* self) {
    // Validate the slot before following any candidate pointer. The array
    // headers are the same fixed ObjectArray/Array declarations already used
    // by the holder probes; CheckInterfaces also checks their runtime classes.
    void* type = class_layout::DecodeClass(art.mirror_class, self, art.class_type);
    ArtField* slot = type ? art.find_instance_field(type, "ifTable", "[Ljava/lang/Object;") : nullptr;
    return slot && slot->declaring_class == Reference(type) &&
        !(slot->access_flags & evolution::kAccStatic) && slot->offset == offsetof(Class, iftable);
  }

  static Class* ReadClass(const VerifiedThread& thread, uint32_t reference) {
    return static_cast<Class*>(runtime_thread::Read(thread, reference));
  }
  // An interface's declared virtual methods occupy consecutive method indices.
  // Probe these indices against its validated ArtMethod array rather than
  // interpreting a DEX index as an IfTable slot (DEX indices are file-local).
  static bool InterfaceMethods(const VerifiedThread& thread, Class* interface, std::vector<Method*>* methods) {
    auto* array = Members<Method>(interface->methods);
    for (uint32_t i = 0; array && i < array->length; ++i) {
      Method* method = array->At(i);
      const uint32_t flags = method->access_flags.load(std::memory_order_relaxed);
      if (ReadClass(thread, method->declaring_class) != interface ||
          (flags & (evolution::kAccStatic | evolution::kAccPrivate))) continue;
      evolution::Member identity;
      std::string why;
      if (!class_layout::MethodIdentity<Class>(thread, method, &identity, &why)) return false;
      if (identity.name == "<init>" || identity.name == "<clinit>") continue;
      if (method->method_index != methods->size()) return false;
      methods->push_back(method);
    }
    return true;
  }
  // A native method pointer must belong to one of the known method arrays
  // before its flags or DEX identity can be read. ART may choose an inherited
  // method or a copied default/miranda owned by the concrete class.
  static bool OwnsMethod(const VerifiedThread& thread, Class* klass, Method* method) {
    for (Class* at = klass; at; at = ReadClass(thread, at->super_class)) {
      auto* methods = Members<Method>(at->methods);
      if (methods && methods->Contains(method)) return true;
    }
    return false;
  }
  static std::string MissingImplementation(const VerifiedThread& thread, const std::string& interface_descriptor,
      const std::string& name, const std::string& descriptor, const std::string& concrete,
      bool submitted, Method* selected, const char* stage = "selected") {
    std::string detail = submitted ? " (submitted; " : " (not submitted; ";
    detail += stage;
    if (selected) {
      detail += " owner=" + class_layout::ClassDescriptor(thread, *ReadClass(thread, selected->declaring_class)) +
          "; flags=" + std::to_string(selected->access_flags.load(std::memory_order_relaxed));
    } else detail += " no dispatch target";
    return interface_descriptor + ": " + name + descriptor +
        " has no concrete public implementation in loaded class " + concrete + detail + ")";
  }
  static bool Submitted(Class* original) {
    for (const auto& [target, requirements] : transaction::Current()->targets)
      if (art.mirror_class(target) == original) return true;
    return false;
  }

  static bool CheckInterfaces(const VerifiedThread& thread, ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                               std::string* rejection) {
    const auto* old_classes = data.Classes(true);
    const auto* new_classes = data.Classes(false);
    if (Obligations().empty() || !old_classes) return true;
    for (const auto& obligation : Obligations()) {
      void* original = art.mirror_class(obligation.interface);
      Class* interface = nullptr;
      for (int32_t i = 0; i < old_classes->length; ++i)
        if (class_layout::Element<void>(thread, old_classes, i) == original)
          interface = class_layout::Element<Class>(thread, new_classes, i);
      if (!interface) continue;  // This obligation belongs to another structural owner.
      // The submitted interface names the rejection; an added parent's method
      // is named with its declaring interface.
      const std::string target = class_layout::ClassDescriptor(thread, *interface);
      std::string method = obligation.name;
      // An unchanged parent keeps its Class in both the old and new IfTables.
      if (obligation.declaring) {
        interface = static_cast<Class*>(class_layout::DecodeClass(art.mirror_class, self, obligation.declaring));
        original = interface;
        method = "added parent " + class_layout::ClassDescriptor(thread, *interface) + "." + obligation.name;
      }
      const auto unavailable = [&](const std::string& why) {
        Report("rejected: " + why);
        *rejection = target + ": " + method + obligation.descriptor + " requires a verified IfTable accessor: " + why;
        return false;
      };
      if (ARTDCEVM_FAULT(21)) return unavailable("injected IfTable probe mismatch");
      if (!DispatchProbe(thread, self)) return unavailable("Class.ifTable field declaration differs");
      std::vector<Method*> declarations;
      if (!InterfaceMethods(thread, interface, &declarations))
        return unavailable("interface method indices differ");
      Method* required = nullptr;
      for (Method* method : declarations) {
        evolution::Member identity;
        std::string why;
        if (!class_layout::MethodIdentity<Class>(thread, method, &identity, &why)) return unavailable(why);
        if (identity.name == obligation.name && identity.descriptor == obligation.descriptor) required = method;
      }
      if (!required) return unavailable("candidate interface declaration is absent");
      void* object_array_type = class_layout::DecodeClass(art.mirror_class, self, object_array_root);
      void* pointer_array_type = class_layout::DecodeClass(art.mirror_class, self, pointer_array_root);
      for (int32_t i = 0; i < new_classes->length; ++i) {
        Class* klass = class_layout::Element<Class>(thread, new_classes, i);
        if (klass->access_flags & (evolution::kAccInterface | evolution::kAccAbstract)) continue;
        auto* table = static_cast<abi::ObjectArray*>(runtime_thread::Read(thread, klass->iftable));
        if (!table || runtime_thread::Read(thread, table->object.klass) != object_array_type ||
            table->length < 0 || table->length % 2) return unavailable("IfTable array declaration differs");
        Method* implementation = nullptr;
        bool found = false;
        for (int32_t entry = 0; entry < table->length; entry += 2) {
          if (ReadClass(thread, table->elements[entry]) != interface) continue;
          if (found) return unavailable("IfTable has duplicate interface identities");
          found = true;
          auto* methods = static_cast<abi::PointerArray*>(
              runtime_thread::Read(thread, table->elements[entry + 1]));
          if (!methods || runtime_thread::Read(thread, methods->object.klass) != pointer_array_type ||
              methods->length != static_cast<int32_t>(declarations.size()))
            return unavailable("IfTable PointerArray declaration differs");
          implementation = Pointer<Method>(methods->elements[required->method_index]);
          if (implementation && !OwnsMethod(thread, klass, implementation) && !OwnsMethod(thread, interface, implementation))
            return unavailable("IfTable target is outside known method arrays");
        }
        // A group can contain siblings of a dependent interface; only types
        // whose actual IfTable contains this candidate identity implement it.
        if (!found) {
          auto* old = class_layout::Element<Class>(thread, old_classes, i);
          auto* old_table = static_cast<abi::ObjectArray*>(runtime_thread::Read(thread, old->iftable));
          if (!old_table || runtime_thread::Read(thread, old_table->object.klass) != object_array_type ||
              old_table->length < 0 || old_table->length % 2)
            return unavailable("old IfTable array declaration differs");
          for (int32_t entry = 0; entry < old_table->length; entry += 2)
            if (runtime_thread::Read(thread, old_table->elements[entry]) == original)
              return unavailable("candidate IfTable is missing an implemented interface");
          continue;
        }
        Report("verified: Class.ifTable, interface indices and arm64 PointerArray");
        evolution::Member identity;
        std::string why;
        const uint32_t flags = implementation ? implementation->access_flags.load(std::memory_order_relaxed) : 0;
        if (!implementation || !(flags & evolution::kAccPublic) ||
            (flags & (evolution::kAccAbstract | evolution::kAccStatic | evolution::kAccPrivate)) ||
            (flags & abi::kAccObsoleteMethod) || member_retirement::IsRetiredMethod(implementation) ||
            IsDefaultConflicting(*implementation) ||
            !class_layout::MethodIdentity<Class>(thread, implementation, &identity, &why) ||
            identity.name != obligation.name || identity.descriptor != obligation.descriptor) {
          *rejection = MissingImplementation(thread, target, method, obligation.descriptor, class_layout::ClassDescriptor(thread, *klass),
              Submitted(class_layout::Element<Class>(thread, old_classes, i)), implementation);
          return false;
        }
      }
    }
    return true;
  }

  static bool NonPublic(const VerifiedThread& thread, ClassRedefinitionPrefix* self, const std::vector<abi::Handle>& originals,
      abi::Handle candidates, int32_t index, NonPublicSelection selection,
      const std::string& concrete_descriptor, bool submitted, std::string* rejection) {
    auto* declaration = reinterpret_cast<Method*>(selection.declaration);
    auto* implementation = reinterpret_cast<Method*>(selection.implementation);
    if (!declaration || !implementation) return false;
    Class* owner = ReadClass(thread, declaration->declaring_class);
    evolution::Member required, selected;
    std::string why;
    if (!class_layout::MethodIdentity<Class>(thread, declaration, &required, &why) ||
        !class_layout::MethodIdentity<Class>(thread, implementation, &selected, &why) ||
        required.name != selected.name || required.descriptor != selected.descriptor) return false;
    const auto* context = transaction::Current();
    for (const auto& obligation : Obligations()) {
      if (obligation.name != required.name || obligation.descriptor != required.descriptor) continue;
      void* original = art.mirror_class(obligation.interface);
      void* declaring = obligation.declaring ? class_layout::DecodeClass(art.mirror_class, self, obligation.declaring)
                                             : nullptr;
      bool candidate = false;
      for (int32_t i = 0; i < index; ++i)
        if (originals[i].Get<void>() == original &&
            (declaring ? declaring == owner
                       : class_layout::Element<Class>(thread, candidates.Get<abi::ObjectArray>(), i) == owner))
          candidate = true;
      if (!candidate) continue;
      const auto* requirement = context->Find(obligation.interface);
      if (!requirement) return false;
      *rejection = MissingImplementation(thread, requirement->descriptor,
          declaring ? "added parent " + class_layout::ClassDescriptor(thread, *owner) + "." + required.name
                    : required.name, required.descriptor,
          concrete_descriptor, submitted, implementation, "ART rejected non-public selection during linking;");
      return true;
    }
    return false;
  }
  static constexpr Ops Make(int) { return {DispatchProbe, CheckInterfaces, NonPublic}; }
};
const auto kVerifiers = class_layout::Table<Verifier, Ops>();
}  // namespace

void Prepare(JNIEnv* jni, const Services& services) {
  art = services;
  for (auto [name, root] : {std::pair{"[Ljava/lang/Object;", &object_array_root},
                            std::pair{"[J", &pointer_array_root}}) {
    jclass type = jni->FindClass(name);
    *root = type ? static_cast<jclass>(jni->NewGlobalRef(type)) : nullptr;
    if (type) jni->DeleteLocalRef(type);
    if (!*root) {
      jni->ExceptionClear();
      for (jclass* allocated : {&object_array_root, &pointer_array_root}) {
        if (*allocated) jni->DeleteGlobalRef(*allocated);
        *allocated = nullptr;
      }
      Report("unavailable: interface dispatch array classes");
      return;
    }
  }
  Report("unverified: IfTable declarations");
}
bool Available(const VerifiedThread& thread, ClassRedefinitionPrefix* self, bool early_link_errors_available) {
  const int selected = class_layout::Selected();
  if (!object_array_root || !pointer_array_root || selected < 0) return false;
  if (!early_link_errors_available) {
    Report("rejected: ART non-public implementation link-error service unavailable");
    return false;
  }
  if (kVerifiers[selected].probe(thread, self)) return true;
  Report("rejected: Class.ifTable field declaration differs");
  return false;
}
std::string State() {
  std::lock_guard<std::mutex> lock(state_mutex);
  return state;
}
void Require(ClassRedefinitionPrefix* interface, std::string name, std::string descriptor, jclass declaring) {
  auto* context = transaction::Current();
  if (!context->dispatch) context->dispatch = std::make_unique<Pending>();
  auto* pending = transaction::PartOf<Pending>(context, &transaction::Context::dispatch);
  pending->entries.push_back({interface, std::move(name), std::move(descriptor), declaring});
}
bool Check(const VerifiedThread& thread, ClassRedefinitionPrefix* self, const RedefinitionDataIter& data, std::string* rejection) {
  const auto* context = transaction::Current();
  if (!context || Obligations().empty()) return true;
  const int selected = class_layout::Selected();
  if (selected >= 0) {
    if (!kVerifiers[selected].check(thread, self, data, rejection)) return false;
    ARTDCEVM_TEST(
    bool owns_condition = false;
    const auto* originals = data.Classes(true);
    for (const auto& obligation : Obligations())
      for (int32_t i = 0; originals && i < originals->length; ++i)
        owns_condition |= class_layout::Element<void>(thread, originals, i) == art.mirror_class(obligation.interface);
    if (owns_condition) fault_injection::Increment(fault_injection::Counter::kValidatedGroups);
    )
    return true;
  }
  *rejection = "added abstract interface methods require a verified IfTable accessor";
  return false;
}
bool DescribeNonPublicSelection(const VerifiedThread& thread, ClassRedefinitionPrefix* self,
    const std::vector<abi::Handle>& originals, abi::Handle candidates, int32_t index,
    NonPublicSelection selection, const std::string& descriptor, bool submitted, std::string* rejection) {
  const int selected = class_layout::Selected();
  return selected >= 0 && kVerifiers[selected].non_public(
      thread, self, originals, candidates, index, selection, descriptor, submitted, rejection);
}
}  // namespace artdcevm::interface_dispatch
