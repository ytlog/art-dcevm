#pragma once
#include <cstdint>
namespace artdcevm::receiver_guard {
inline constexpr char kCheckedResolveMethod[]="_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE";
inline constexpr char kCheckedResolveMethodCurrent[]="_ZN3art11ClassLinker23ResolveMethodWithChecksEjPNS_9ArtMethodENS_10InvokeTypeE";
// Fixed class_linker-inl.h: the complete checked leaf reads Thread::Current
// internally. ObjPtr/template type resolution is not required by this ABI.
using CheckedResolveMethodCurrent=void*(*)(void*,uint32_t,void*,uint32_t);
enum class ResolutionSource {CheckedMethod,CheckedMethodCurrent,Nterp};
// Exact source leaves select a candidate only. Their successful binding still
// requires independent public caller/target/receiver episodes before use.
// Scanner geometry intentionally does not participate in this decision.
inline bool SelectResolutionSource(bool checked_method,bool resolve_type,
                                   bool find_member,bool nterp,bool native_pending,
                                   ResolutionSource*out,const char**error,bool checked_current=false)noexcept {
 if(!out)return false;
 if(checked_method){*out=ResolutionSource::CheckedMethod;return true;}
 if(checked_current){if(!native_pending){if(error)*error="Receiver checked source calibrated native pending reader unavailable";return false;}*out=ResolutionSource::CheckedMethodCurrent;return true;}
 const char* failure=!resolve_type?"Receiver source named DoResolveType<ArtMethod> unavailable":
                     !find_member?"Receiver source named FindResolvedMethod unavailable":
                     !nterp?"Receiver source named NterpGetMethod unavailable":
                     !native_pending?"Receiver source calibrated native pending reader unavailable":nullptr;
 if(failure){if(error)*error=failure;return false;}
 *out=ResolutionSource::Nterp;return true;
}
}
