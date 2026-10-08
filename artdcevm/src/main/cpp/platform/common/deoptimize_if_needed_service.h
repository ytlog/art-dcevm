#pragma once
#include "platform/abi/arm64/android16/deoptimize_if_needed.h"
namespace artdcevm::deoptimize_if_needed_service {
namespace abi=deoptimize_if_needed_abi;
inline constexpr const char* kSymbol="_ZN3art15instrumentation15Instrumentation18DeoptimizeIfNeededEPNS_6ThreadEPPNS_9ArtMethodENS_24DeoptimizationMethodTypeENS_6JValueEb";
// Explicit calibrated signature selection; no SDK inference or symbol fallback.
// Fixed 11–13 r1 lack this service; 14–15 return void; 16–17 own Context.
enum class Declaration {kAbsent,kReturnsVoid,kReturnsOwnedContext};
class Service {
 public:
  static bool Bind(Declaration declaration,void* symbol,Service* out){
    if(!out||!symbol||declaration!=Declaration::kReturnsVoid)return false;
    Service value;value.void_=reinterpret_cast<abi::ReturnsVoid>(symbol);
    *out=value;return true;
  }
  // Requires the original ART quick stack and mutator context, correct return
  // value/ref kind and ShouldDeoptimizeCaller marking. This is a conditional
  // deopt request, never an unconditional replacement for artDeoptimize.
  bool InvokeVoid(void* instrumentation,void* thread,void** sp,abi::MethodType type,abi::JValue value,bool is_ref)const{
    if(!void_||!instrumentation||!thread||!sp||!Valid(type))return false;
    void_(instrumentation,thread,sp,type,value,is_ref);return true;
  }
  // Owned-return declarations are deliberately refused. A provider needs a
  // calibrated Context-consuming long jump before requesting the modern call;
  // a non-null result already changed the stack and cannot simply be discarded.
 private:
  static bool Valid(abi::MethodType type){return type==abi::MethodType::kKeepDexPc||type==abi::MethodType::kDefault;}
  abi::ReturnsVoid void_=nullptr;
};
}  // namespace artdcevm::deoptimize_if_needed_service
