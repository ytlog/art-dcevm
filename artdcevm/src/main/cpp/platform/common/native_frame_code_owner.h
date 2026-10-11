#pragma once
#include <cstdint>
namespace artdcevm::native_frame_code_owner {
enum class Kind {Unknown,Nterp};
// Actual named ArtMethod/header identity for the noncompiled Nterp reader.
class Service {
 public:
 template<class Finder>static Service Bind(Finder finder){Service s;
  s.method_header_=reinterpret_cast<const void*(*)(void*,uintptr_t)>(finder(kMethodHeader));
  s.nterp_=reinterpret_cast<bool(*)(const void*)>(finder(kNterp));return s;}
 Kind Observe(void*method,uintptr_t pc,const void*header)const noexcept {
  return method&&pc&&header&&method_header_&&nterp_&&
   method_header_(method,pc)==header&&nterp_(header)?Kind::Nterp:Kind::Unknown;
 }
 bool ExactHeaderAvailable()const noexcept{return method_header_&&nterp_;}
 inline static constexpr char kMethodHeader[]="_ZN3art9ArtMethod23GetOatQuickMethodHeaderEm";
 inline static constexpr char kNterp[]="_ZNK3art20OatQuickMethodHeader19IsNterpMethodHeaderEv";
 private:
 const void*(*method_header_)(void*,uintptr_t)=nullptr;
 bool(*nterp_)(const void*)=nullptr;
};
}
