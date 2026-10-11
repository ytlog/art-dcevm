#include "platform/common/native_source_class_loading.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include "platform/common/method_identity_calibration.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace artdcevm::native_source_class_loading {
namespace {
// Fixed source single polymorphic ClassLoadCallback base, followed by self and
// is_running in ClassDefinitionPauser. Itanium address-point is vtable + 2 words.
struct PauserPrefix {const void*vtable;void*self;uint8_t running;};
static_assert(offsetof(PauserPrefix,self)==8&&offsetof(PauserPrefix,running)==16);
bool Mapped(const void*p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
thread_local Run*current=nullptr;
using Callback=void(*)(void*,void*);
Callback add_original=nullptr,remove_original=nullptr;Service*installed=nullptr;
std::mutex installation;
void Error(std::string*e,const char*s){if(e)*e=s;}
}
namespace detail {struct Hooks {
static void Add(void*callbacks,void*pauser){
 // Record the typed source argument while still inside the native constructor.
 // Completion is deliberately deferred to entry of the real first Finish boundary.
 if(auto*run=Run::Current())run->Added(callbacks,pauser);
 add_original(callbacks,pauser);
}
static void Remove(void*callbacks,void*pauser){
 auto*run=Run::Current();
 const bool opened=run&&run->Removing(callbacks,pauser);
 try{remove_original(callbacks,pauser);}
 catch(...){if(opened)run->Removed(callbacks,pauser,false);throw;}
 if(opened)run->Removed(callbacks,pauser,true);
}
};}
bool Service::Bind(HookEngine&hooks,const runtime_services::Snapshot&s,Service*out,std::string*error){
 if(!out||!s.runtime){Error(error,"Source class loading requires independently observed Runtime");return false;}
 auto*table=hooks.FindSymbol("libopenjdkjvmti.so","_ZTVN12openjdkjvmti21ClassDefinitionPauserE");
 if(!table||!Mapped(table,3*sizeof(void*))){Error(error,"Missing named source ClassDefinitionPauser vtable");return false;}
 if(installed==out){Error(error,"Cannot rebind an installed source class-loading owner");return false;}
 out->runtime_=s.runtime;out->vtable_=static_cast<const unsigned char*>(table)+2*sizeof(void*);out->observed_.store(false,std::memory_order_release);return true;
}
Run::Run(Service&s,void*driver,const runnable_jni::Context&ctx):service_(s),driver_(driver),context_(ctx),previous_(current){
 using abi::arm64::android_common::RedefinerPrefix;
 if(current||installed!=&s||!s.runtime_||!s.vtable_||!ctx.env||!ctx.self||!Mapped(driver,sizeof(RedefinerPrefix)))return;
 RedefinerPrefix d;std::memcpy(&d,driver,sizeof(d));
 if(!d.environment||d.runtime!=s.runtime_||d.self!=ctx.self||d.type!=0||d.result!=JVMTI_ERROR_INTERNAL)return;
 current=this;active_=true;
}
Run::~Run(){if(active_){if(current!=this||!complete_)std::abort();current=previous_;}}
Run*Run::Current()noexcept{return current;}
void Run::Added(void*callbacks,void*pauser)noexcept{
 if(!active_||!Mapped(pauser,sizeof(PauserPrefix)))return;
 PauserPrefix p;std::memcpy(&p,pauser,sizeof(p));
 // Unrelated callback implementations preserve their original behavior.
 if(p.vtable!=service_.vtable_||p.self!=context_.self)return;
 if(!callbacks||p.running!=1||added_||removal_!=Removal::kNone){failed_=true;return;}
 callbacks_=callbacks;pauser_=pauser;added_=true;
}
bool Run::BeforeFinish(void*owner,const transaction_root::Holder*h,const void*raw)noexcept{
 using abi::arm64::android_common::ClassRedefinitionPrefix;
 transaction_root::Iterator iter{};
 if(!active_||failed_||!added_||removal_!=Removal::kNone||finish_owner_||!Mapped(raw,sizeof(iter))||!transaction_root::ReadIterator(raw,sizeof(iter),&iter)||iter.holder!=h||iter.index!=next_||!Mapped(owner,sizeof(ClassRedefinitionPrefix))){failed_=true;return false;}
 transaction_root::Holder header{};transaction_root::Owners owners{};
 if(!Mapped(h,sizeof(header))||!transaction_root::ReadHolderHeader(h,sizeof(header),&header)||!Mapped(header.redefinitions,sizeof(owners))){failed_=true;return false;}
 std::memcpy(&owners,header.redefinitions,sizeof(owners));
 const uintptr_t begin=uintptr_t(owners.begin),end=uintptr_t(owners.end),capacity=uintptr_t(owners.capacity);
 if(!begin||begin%8||end<begin||capacity<end||(end-begin)%sizeof(ClassRedefinitionPrefix)||size_t(iter.index)>=(end-begin)/sizeof(ClassRedefinitionPrefix)||owner!=owners.begin+sizeof(ClassRedefinitionPrefix)*size_t(iter.index)){failed_=true;return false;}
 ClassRedefinitionPrefix def;std::memcpy(&def,owner,sizeof(def));
 if(def.driver!=driver_||(holder_&&holder_!=h)){failed_=true;return false;}
 // Holder/owner vector partitions are validated separately by the sole source
 // reader. This observer only proves exact Run ordering of the source pause.
 // The original Run constructs ScopedSuspendClassLoading before entering its
 // allocation loop. Reaching this exact first Finish leaf therefore proves the
 // constructor's WaitFor has returned, before Finish performs private allocation.
 // AddCallback alone, or successful completion of the allocation, is not that
 // boundary. Recheck the still-live exact pauser rather than trusting Add state.
 if(!Mapped(pauser_,sizeof(PauserPrefix))){failed_=true;return false;}
 PauserPrefix pauser;std::memcpy(&pauser,pauser_,sizeof(pauser));
 if(pauser.vtable!=service_.vtable_||pauser.self!=context_.self||pauser.running!=1){failed_=true;return false;}
 holder_=h;finish_owner_=owner;wait_complete_=true;return true;
}
void Run::AfterFinish(void*owner,bool success)noexcept{
 if(!finish_owner_||finish_owner_!=owner)std::abort();
 finish_owner_=nullptr;if(success)++next_;else failed_=true;
}
bool Run::OwnsDriver(const void*driver)const noexcept{return active_&&!complete_&&current==this&&driver==driver_;}
bool Run::OwnsFinishedHolder(const void*holder,size_t count)const noexcept{
 return Paused()&&!finish_owner_&&holder&&holder==holder_&&count&&size_t(next_)==count;
}
bool Run::Paused()const noexcept{return active_&&!failed_&&added_&&wait_complete_&&removal_==Removal::kNone&&current==this;}
bool Run::RefundWindow()const noexcept{
 if(!active_||complete_||!added_||!wait_complete_||finish_owner_||removal_!=Removal::kNone||current!=this||!Mapped(pauser_,sizeof(PauserPrefix)))return false;
 PauserPrefix p;std::memcpy(&p,pauser_,sizeof(p));
 return p.vtable==service_.vtable_&&p.self==context_.self&&p.running==1;
}
bool Run::BeginCleanup(void* carrier)noexcept{
 if(!Paused()||finish_owner_||!carrier||cleanup_started_||!Mapped(pauser_,sizeof(PauserPrefix))){failed_=true;return false;}
 PauserPrefix p;std::memcpy(&p,pauser_,sizeof(p));
 if(p.vtable!=service_.vtable_||p.self!=context_.self||p.running!=1){failed_=true;return false;}
 cleanup_carrier_=carrier;cleanup_started_=true;return true;
}
bool Run::EndCleanup(void* carrier)noexcept{
 if(!active_||failed_||current!=this||!cleanup_started_||cleanup_ended_||carrier!=cleanup_carrier_||removal_!=Removal::kReturned){failed_=true;return false;}
 cleanup_carrier_=nullptr;cleanup_ended_=true;return true;
}
bool Run::CleanupCompleted()const noexcept{return active_&&!failed_&&current==this&&cleanup_started_&&cleanup_ended_&&removal_==Removal::kReturned;}
bool Run::Removing(void*callbacks,void*pauser)noexcept{
 if(!active_||!added_||pauser!=pauser_)return false;
 if(callbacks!=callbacks_||removal_!=Removal::kNone||finish_owner_||(cleanup_started_&&(!cleanup_carrier_||cleanup_ended_))){failed_=true;return false;}
 // Release has already started. No new pause permission while the real Remove
 // is executing; its entry alone is not proof of successful callback removal.
 removal_=Removal::kPending;return true;
}
void Run::Removed(void*callbacks,void*pauser,bool returned)noexcept{
 if(current!=this||removal_!=Removal::kPending||callbacks!=callbacks_||pauser!=pauser_)std::abort();
 removal_=returned?Removal::kReturned:Removal::kUnwound;
 if(!returned)failed_=true;
 // Do not read borrowed source storage after the original call or unwind.
 pauser_=nullptr;callbacks_=nullptr;
}
bool Run::Complete(bool success)noexcept{
 if(!active_)return false;
 if(removal_==Removal::kPending)return false;
 if(complete_||finish_owner_)std::abort();
 // No borrowed pauser/Holder storage survives source Run destruction.
 const bool proof=success&&!failed_&&wait_complete_&&next_>0&&removal_==Removal::kReturned&&(!cleanup_started_||cleanup_ended_);
 if(proof)service_.observed_.store(true,std::memory_order_release);
 if(added_&&removal_==Removal::kNone)std::abort();
 complete_=true;holder_=nullptr;return proof;
}
bool Install(HookEngine&hooks,Service&s,std::string*error){
 std::lock_guard<std::mutex>lock(installation);if(installed)return installed==&s;
 if(!s.runtime_||!s.vtable_)return false;
 auto*a=hooks.FindSymbol("libart.so","_ZN3art16RuntimeCallbacks20AddClassLoadCallbackEPNS_17ClassLoadCallbackE");
 auto*r=hooks.FindSymbol("libart.so","_ZN3art16RuntimeCallbacks23RemoveClassLoadCallbackEPNS_17ClassLoadCallbackE");
 if(!a||!r){Error(error,"Missing named source class-load callback boundaries");return false;}
 auto*first=hooks.Install(a,reinterpret_cast<void*>(detail::Hooks::Add),reinterpret_cast<void**>(&add_original),error);
 if(first&&!add_original)std::abort();
 auto*second=first?hooks.Install(r,reinterpret_cast<void*>(detail::Hooks::Remove),reinterpret_cast<void**>(&remove_original),error):nullptr;
 if(second&&!remove_original)std::abort();
 if(first&&!second){if(!hooks.Remove(first,nullptr))std::abort();add_original=nullptr;}
 if(!first||!second)return false;installed=&s;return true;
}
}
