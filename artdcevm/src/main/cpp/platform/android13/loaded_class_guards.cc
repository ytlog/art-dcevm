#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/loaded_class_guards_abi.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/modifier_evolution_policy.h"
#include "platform/android13/validation_metadata.h"
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
uint32_t Flags(void* klass) { return Read<uint32_t>(klass, offsetof(Class, access_flags)); }
void* Parent(void* klass) { return ReadHeapReference(klass, offsetof(Class, super_class)); }
bool Interface(void* klass) { return (Flags(klass) & 0x200) != 0; }
bool Subclass(void* src, void* target) {
  for (; src != nullptr; src = Parent(src)) if (src == target) return true;
  return false;
}
bool Assignable(void* target, void* src) {
  if (target == src) return true;
  if ((Read<uint32_t>(target, offsetof(Class, primitive_type)) & 0xffff) == 0 && !Parent(target))
    return (Read<uint32_t>(src, offsetof(Class, primitive_type)) & 0xffff) == 0;
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
struct Dex {
  evolution::MappedDexMetadata mapping;
  void Range(size_t at, size_t width, size_t size) const { Require(at <= size && width <= size - at); }
  uint32_t U32(const unsigned char* bytes, size_t size, size_t at) const {
    Range(at, 4, size); return uint32_t{bytes[at]} | uint32_t{bytes[at+1]} << 8 |
        uint32_t{bytes[at+2]} << 16 | uint32_t{bytes[at+3]} << 24;
  }
  uint16_t U16(const unsigned char* bytes, size_t size, size_t at) const {
    Range(at, 2, size); return uint16_t(bytes[at] | uint16_t{bytes[at+1]} << 8);
  }
  uint32_t Main32(size_t at) const { return U32(mapping.begin, mapping.size, at); }
  size_t Id(size_t header, uint32_t index, size_t width) const {
    const uint32_t count = Main32(header), at = Main32(header+4);
    Require(index < count && at >= 0x70 && at % 4 == 0);
    Range(at, size_t{count} * width, mapping.size); return at + size_t{index} * width;
  }
  const char* String(uint32_t index) const {
    size_t at = Main32(Id(56, index, 4)); bool complete = false;
    for (unsigned i = 0; i != 5; ++i) {
      Range(at, 1, mapping.data_size); unsigned byte = mapping.data_begin[at++];
      if (i == 4) Require((byte & 0xf0) == 0);
      if (!(byte & 0x80)) { complete = true; break; }
    }
    Require(complete); Range(at, 1, mapping.data_size);
    Require(std::memchr(mapping.data_begin+at, 0, mapping.data_size-at));
    return reinterpret_cast<const char*>(mapping.data_begin+at);
  }
  const char* Type(uint32_t index) const { return String(Main32(Id(64,index,4))); }
  bool ConsumeType(uint32_t index, const char** requested) const {
    const char* actual = Type(index); size_t length = std::strlen(actual);
    if (std::strncmp(*requested, actual, length) != 0) return false;
    *requested += length; return true;
  }
  bool Matches(uint32_t index, const char* name, const char* signature) const {
    if (index == UINT32_MAX) return false; // runtime methods have no Java signature
    size_t method = Id(88,index,8);
    if (std::strcmp(String(Main32(method+4)),name) != 0 || *signature++ != '(') return false;
    uint32_t proto_index = U16(mapping.begin,mapping.size,method+2);
    size_t proto = Id(72,proto_index,12); uint32_t parameters = Main32(proto+8);
    if (parameters) {
      uint32_t count = U32(mapping.data_begin,mapping.data_size,parameters);
      Range(size_t{parameters}+4,size_t{count}*2,mapping.data_size);
      for (uint32_t i=0;i<count;++i)
        if (!ConsumeType(U16(mapping.data_begin,mapping.data_size,size_t{parameters}+4+size_t{i}*2),&signature)) return false;
    }
    return *signature++ == ')' && ConsumeType(Main32(proto+4),&signature) && *signature == '\0';
  }
};
bool Matches(void* method, void* declaring, const char* name, const char* signature) {
  const uint32_t index = Read<uint32_t>(method,offsetof(Method,dex_method_index));
  if (index == UINT32_MAX) return false;
  ValidationMetadata metadata{}; Require(ReadValidationClassMetadata(declaring,&metadata));
  return Dex{metadata.dex}.Matches(index,name,signature);
}
template<class Fn> bool Methods(void* klass, Fn action) {
  auto* array = reinterpret_cast<void*>(Read<uint64_t>(klass,offsetof(Class,methods)));
  if (!array) return false;
  uint32_t count = Read<uint32_t>(array,0);
  uint16_t start = Read<uint16_t>(klass,offsetof(Class,virtual_methods_offset)); Require(start <= count);
  // Includes copied default/miranda methods, as GetVirtualMethods does.
  for (size_t i=start;i<count;++i) {
    void* method = static_cast<unsigned char*>(array)+art_method_abi::kArrayDataOffset+i*sizeof(Method);
    if (action(method)) return true;
  }
  return false;
}
class Guard final : public loaded_class_abi::ClassVisitor {
 public:
  Guard(void* target, const char* name, const char* signature, uint32_t flags, int action)
      : target_(target),name_(name),signature_(signature),flags_(flags),action_(action) { Require(target != nullptr); }
  bool invalid = false;
  bool operator()(uintptr_t value) override {
    auto* subtype = reinterpret_cast<void*>(value);
    uint32_t status = __atomic_load_n(reinterpret_cast<const uint32_t*>(
        static_cast<unsigned char*>(subtype)+offsetof(Class,status)),__ATOMIC_SEQ_CST);
    if (subtype == target_ || (status >> 28) < 5 || !Assignable(target_,subtype)) return true;
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
bool CheckMethodDependentGuard(void* definition,const char* name,const char* descriptor,uint32_t old_flags,int action) {
  Guard guard(GetValidationClass(definition),name,descriptor,old_flags,action); visit(linker,&guard); return !guard.invalid;
}
}
