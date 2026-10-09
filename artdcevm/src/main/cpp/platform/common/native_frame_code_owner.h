#pragma once
#include <cstdint>
namespace artdcevm::native_frame_code_owner {
enum class Kind {Unknown,Jit,Aot,Nterp};
// Fixed ArtMethod::GetOatQuickMethodHeader(uintptr_t), stack.cc and
// oat_quick_method_header.h. AOT ownership comes from the actual source method
// and current PC; a readable address or absence from the JIT cache is insufficient.
class Service {
 public:
 template<class Finder>static Service Bind(Finder finder){
  Service s;
  s.method_header_=reinterpret_cast<const void*(*)(void*,uintptr_t)>(finder(kMethodHeader));
  s.contains_=reinterpret_cast<bool(*)(const void*,uintptr_t)>(finder(kContains));
  s.jit_=reinterpret_cast<bool(*)(const void*,const void*)>(finder(kJit));
  s.nterp_=reinterpret_cast<bool(*)(const void*)>(finder(kNterp));return s;
 }
 Kind Observe(void* physical_method,uintptr_t pc,const void* actual_header,void* jit)const noexcept {
  if(!physical_method||!pc||!actual_header)return Kind::Unknown;
  if(jit&&jit_&&jit_(jit,reinterpret_cast<const void*>(pc)))
   return nterp_&&nterp_(actual_header)?Kind::Unknown:Kind::Jit;
  if(!method_header_||!nterp_||method_header_(physical_method,pc)!=actual_header||
     (contains_&&!contains_(actual_header,pc)))return Kind::Unknown;
  if(nterp_&&nterp_(actual_header))return Kind::Nterp;
  return Kind::Aot;
 }
 bool ExactHeaderAvailable()const noexcept{return method_header_!=nullptr;}
 inline static constexpr char kMethodHeader[]="_ZN3art9ArtMethod23GetOatQuickMethodHeaderEm";
 inline static constexpr char kContains[]="_ZNK3art20OatQuickMethodHeader8ContainsEm";
 inline static constexpr char kJit[]="_ZNK3art3jit12JitCodeCache10ContainsPcEPKv";
 inline static constexpr char kNterp[]="_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv";
 private:
 const void*(*method_header_)(void*,uintptr_t)=nullptr;
 bool(*contains_)(const void*,uintptr_t)=nullptr;
 bool(*jit_)(const void*,const void*)=nullptr;bool(*nterp_)(const void*)=nullptr;
};
}
