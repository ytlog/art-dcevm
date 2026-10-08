#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/loaded_class_guards_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "platform/android13/validation_metadata.h"
#include "platform/common/method_array_view.h"
#include <cstdlib>
#include <cstring>
namespace artdcevm::android13 {
namespace {
using Class = validation_abi::ClassPrefix;
using Method = art_method_abi::ArtMethod;
void* linker = nullptr;
loaded_class_abi::VisitClasses visit = nullptr;
loaded_class_abi::SamePackage same_package = nullptr;
loaded_class_abi::ReadRoot read_root = nullptr;
void Require(bool value) { if (!value) std::abort(); }
template<class T> T Read(const void* object, size_t offset) {
  T value; std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value)); return value;
}
class_metadata::State State(void* klass) {
  class_metadata::State state;
  Require(class_metadata::ReadState(33,klass,sizeof(Class),&state));
  return state;
}
uint32_t Flags(void* klass) { return State(klass).access_flags; }
void* Parent(void* klass) { return ReadHeapReference(klass, offsetof(Class, super_class)); }
bool Interface(void* klass) { return (Flags(klass) & 0x200) != 0; }
bool Subclass(void* src, void* target) {
  for (; src != nullptr; src = Parent(src)) if (src == target) return true;
  return false;
}
bool Assignable(void* target, void* src) {
  if (target == src) return true;
  if (!State(target).IsPrimitive() && !Parent(target))
    return !State(src).IsPrimitive();
  if (Interface(target)) {
    auto* table = ReadHeapReference(src, offsetof(Class, iftable));
    if (!table) return false;
    const int32_t length = Read<int32_t>(table, 8); Require(length >= 0 && length % 2 == 0);
    for (int32_t i = 0; i < length; i += 2)
      if (ReadHeapReference(table, 12 + static_cast<size_t>(i) * 4) == target) return true;
    return false;
  }
  auto* source_component = ReadHeapReference(src, offsetof(Class, component_type));
  if (source_component) {
    auto* target_component = ReadHeapReference(target, offsetof(Class, component_type));
    return target_component ? Assignable(target_component, source_component) : target == Parent(src);
  }
  return !Interface(src) && Subclass(src, target);
}
bool Accessible(void* subtype, void* target, uint32_t flags) {
  if (subtype == target || (flags & 1)) return true;
  if (flags & 2) return false;
  if ((flags & 4) && !Interface(subtype) && Subclass(subtype, target)) return true;
  return same_package(subtype, reinterpret_cast<uintptr_t>(target));
}
bool Matches(void* method, void* declaring, const char* name, const char* signature) {
  const uint32_t index = Read<uint32_t>(method,offsetof(Method,dex_method_index));
  if (index == UINT32_MAX) return false;
  ValidationMetadata metadata{}; Require(ReadValidationClassMetadata(declaring,&metadata));
  bool matched = false;
  std::string error;
  Require(evolution::ReadMappedDexMethodMatches(metadata.dex,index,name,signature,&matched,&error));
  return matched;
}
template<class Fn> bool Methods(void* klass, Fn action) {
  method_array::View view;
  Require(method_array::ReadTrustedClass(33,klass,sizeof(Class),method_array::Kind::Ordinary,nullptr,&view));
  return view.VisitVirtual(action);
}
class Guard final : public loaded_class_abi::ClassVisitor {
 public:
  Guard(void* target, const char* name, const char* signature, uint32_t flags, int action)
      : target_(target),name_(name),signature_(signature),flags_(flags),action_(action) { Require(target != nullptr); }
  bool invalid = false;
  bool operator()(uintptr_t value) override {
    auto* subtype = reinterpret_cast<void*>(value);
    if (subtype == target_ || State(subtype).status < 5 || !Assignable(target_,subtype)) return true;
    if (!name_) { invalid = true; return false; }
    if (action_ == static_cast<int>(ModifierEvolutionAction::kCheckFinalOverride)) {
      if (!Accessible(subtype,target_,flags_)) return true;
      invalid = Methods(subtype,[&](void* method) {
        return Matches(method,read_root(method),name_,signature_);
      });
    } else {
      if (Flags(subtype)&0x400) return true;
      bool concrete = false;
      for (auto* cursor=subtype; cursor && cursor != target_; cursor=Parent(cursor)) {
        // Stop on the first matching declaration in each class, but an abstract
        // declaration does NOT stop ancestor search: this is the existing guard.
        Methods(cursor,[&](void* method) {
          auto* declaring = read_root(method);
          if (declaring != cursor || !Matches(method,declaring,name_,signature_)) return false;
          concrete = (__atomic_load_n(reinterpret_cast<const uint32_t*>(
              static_cast<unsigned char*>(method)+offsetof(Method,access_flags)),__ATOMIC_RELAXED)&0x400)==0;
          return true;
        });
        if (concrete) break;
      }
      invalid = !concrete;
    }
    return !invalid;
  }
 private:
  void* target_; const char* name_; const char* signature_; uint32_t flags_; int action_;
};
}
bool ConfigureLoadedClassGuards(HookEngine& hooks, void* class_linker, std::string* error) {
  auto v = reinterpret_cast<loaded_class_abi::VisitClasses>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker12VisitClassesEPNS_12ClassVisitorE"));
  auto p = reinterpret_cast<loaded_class_abi::SamePackage>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Class15IsInSamePackageENS_6ObjPtrIS1_EE"));
  auto r = reinterpret_cast<loaded_class_abi::ReadRoot>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  if (!class_linker || !v || !p || !r) { *error="Missing named loaded-class visitor, package check or root reader"; return false; }
  linker=class_linker; visit=v; same_package=p; read_root=r; return true;
}
bool CheckLoadedSubclassGuard(void* definition) {
  Guard guard(GetValidationClass(definition),nullptr,nullptr,0,0); visit(linker,&guard); return !guard.invalid;
}
bool IsValidationAssignableFrom(void* target, void* source) { return Assignable(target,source); }
bool IsValidationSubclassOf(void* source, void* target) { return Subclass(source,target); }
bool CanAccessValidationClass(void* source, void* target) {
  return (Flags(target)&1) || same_package(source,reinterpret_cast<uintptr_t>(target));
}
bool CanAccessValidationMember(void* source,void* declaring,uint32_t flags) { return Accessible(source,declaring,flags); }
void* ReadValidationMethodClass(void* method) { return read_root(method); }
bool CheckMethodDependentGuard(void* definition,const char* name,const char* descriptor,uint32_t old_flags,int action) {
  Guard guard(GetValidationClass(definition),name,descriptor,old_flags,action); visit(linker,&guard); return !guard.invalid;
}
}
