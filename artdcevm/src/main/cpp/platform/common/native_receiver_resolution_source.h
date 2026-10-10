#pragma once
namespace artdcevm::receiver_guard {
inline constexpr char kCheckedResolveMethod[]="_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE";
enum class ResolutionSource {CheckedMethod,Nterp};
// Exact source leaves select a candidate only. Their successful binding still
// requires independent public caller/target/receiver episodes before use.
// Scanner geometry intentionally does not participate in this decision.
inline bool SelectResolutionSource(bool checked_method,bool resolve_type,
                                   bool find_member,bool nterp,bool actual_vm,
                                   ResolutionSource*out,const char**error)noexcept {
 if(!out)return false;
 if(checked_method){*out=ResolutionSource::CheckedMethod;return true;}
 const char* failure=!resolve_type?"Receiver source named DoResolveType<ArtMethod> unavailable":
                     !find_member?"Receiver source named FindResolvedMethod unavailable":
                     !nterp?"Receiver source named NterpGetMethod unavailable":
                     !actual_vm?"Receiver source actual JavaVM unavailable":nullptr;
 if(failure){if(error)*error=failure;return false;}
 *out=ResolutionSource::Nterp;return true;
}
}
