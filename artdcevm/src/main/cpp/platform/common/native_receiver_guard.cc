#include "platform/common/native_receiver_guard.h"
#include <cstring>
#include <cstdlib>
namespace artdcevm::receiver_guard {
namespace {constexpr uint32_t complete=(uint32_t{1}<<static_cast<unsigned>(Path::Count))-1;bool Same(const Invocation&a,const Invocation&b){return a.path==b.path&&a.thread==b.thread&&a.member==b.member&&a.receiver==b.receiver&&a.special_caller==b.special_caller&&a.field==b.field;}}
Guard::Guard(Access access):access_(access){}
bool Guard::BeginProof(const Oracle& oracle)noexcept{
 std::lock_guard<std::mutex> lock(lock_);if(probing_||Active()||!oracle.invocation.thread||!oracle.invocation.member||!oracle.invocation.receiver||!oracle.declaring||!oracle.receiver_class||oracle.invocation.path>=Path::Count)return false;
 oracle_=oracle;probing_=true;seen_=rejected_=false;return true;
}
bool Guard::EndProof(bool succeeded,bool incompatible,bool unchanged)noexcept{
 std::lock_guard<std::mutex> lock(lock_);if(!probing_)return false;
 const bool okay=seen_&&(oracle_.rejection?(rejected_&&!succeeded&&incompatible&&unchanged):(!rejected_&&succeeded&&!incompatible));
 if(okay)(oracle_.rejection?refused_:passed_)|=uint32_t{1}<<static_cast<unsigned>(oracle_.invocation.path);
 probing_=false;oracle_={};return okay;
}
bool Guard::Proven()const noexcept{std::lock_guard<std::mutex> lock(lock_);return !probing_&&passed_==complete&&refused_==complete;}
void Guard::Activate()noexcept{if(!Proven())std::abort();active_.store(true,std::memory_order_release);}
bool Guard::Check(const Invocation& call)noexcept{
 Oracle expected{};bool probe=false;
 {std::lock_guard<std::mutex> lock(lock_);probe=probing_&&Same(call,oracle_.invocation);if(probe)expected=oracle_;}
 if(!Active()&&!probe)return false;
 const auto fail=[&](const char*type,const char*message){if(call.result){const uint64_t zero=0;std::memcpy(call.result,&zero,8);}if(!access_.raise||!call.thread)std::abort();access_.raise(access_.owner,call.thread,type,message);return true;};
 if(call.path>=Path::Count||!access_.current||!access_.member||!access_.object_class||!access_.normalize||!access_.assignable||!access_.raise||!access_.current(access_.owner,call.thread))return fail("Ljava/lang/IllegalStateException;","Receiver guard lost calibrated mutator context");
 uint32_t flags=0;void *declaring=nullptr,*receiver_class=nullptr,*selected=nullptr;
 if(!call.member||!access_.member(access_.owner,call.member,call.field,&flags,&declaring)||!declaring)return fail("Ljava/lang/IllegalStateException;","Receiver guard lost exact native member identity");
 // Preserve static and null receiver semantics. ART retains its own NPE/type
 // handling; a static Class argument must never be decoded as an instance.
 if(flags&8u||!call.receiver)return false;
 if(!access_.object_class(access_.owner,call.receiver,&receiver_class)||!receiver_class||!access_.normalize(access_.owner,call.thread,declaring,receiver_class,&selected)||!selected)return fail("Ljava/lang/IllegalStateException;","Receiver guard lost current Class alias");
 bool related=false;if(!access_.assignable(access_.owner,selected,receiver_class,&related))return fail("Ljava/lang/IllegalStateException;","Receiver guard could not prove native assignability");
 if(related&&call.path==Path::HandleSpecial){void*caller=nullptr;if(!call.special_caller||!access_.special||!access_.normalize(access_.owner,call.thread,call.special_caller,receiver_class,&caller)||!caller||!access_.special(access_.owner,selected,caller,&related))return fail("Ljava/lang/IllegalStateException;","Special MethodHandle caller relation could not be proved");}
 if(probe){std::lock_guard<std::mutex> lock(lock_);if(probing_&&Same(call,oracle_.invocation)&&declaring==expected.declaring&&receiver_class==expected.receiver_class){seen_=true;rejected_=!related;}}
 if(related)return false;
 return fail("Ljava/lang/IncompatibleClassChangeError;","Receiver lost its member declaring Class relationship");
}
}
