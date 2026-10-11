#include "platform/common/production_final_dex.h"
#include "platform/common/native_transformed_definitions.h"
#include <atomic>
#include <mutex>
#include <new>

namespace artdcevm::production_bootstrap {
bool FinalDex::Install(HookEngine&hooks,JavaVM*vm,std::string*error){
 const auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(!vm||(vm_&&vm_!=vm))return fail("final-DEX-observer-VM-or-source-invalid");
 if(!current_){
  if(!hooks.Initialize(error))return false;
  current_=reinterpret_cast<abi::common::CurrentThread>(hooks.FindSymbol("libart.so",abi::common::kCurrentThread));
  if(!current_)return fail("final-DEX-observer-original-current-thread-unavailable");
 }
 if(!vm_)vm_=vm;
 if(!redefinition_gate::InstallDirectObservation(hooks,&observer_,error))return false;
 redefinition_gate::Lease lease(vm);
 if(!lease.Acquired())return fail("final-DEX-observer-history-lease-unavailable");
 if(!records_.Trusted())return fail("final-DEX-observer-source-invalidated");
 if(!redefinition_gate::ObservationComplete(vm,&observer_)){
  records_.Invalidate();return fail("final-DEX-observer-missed-Direct-source-history");
 }
 return true;
}
Provenance*FinalDex::Borrow(JavaVM*vm)const noexcept{
 if(vm!=vm_||!redefinition_gate::ObservationComplete(vm,&observer_)||!records_.Trusted())return nullptr;
 return &records_;
}
namespace {
struct ProcessFinalDex {
 Provenance records;
 FinalDex observer{records};
};
std::mutex installation;
std::atomic<ProcessFinalDex*>published{nullptr};
}
bool InstallFinalDexAtStartup(HookEngine&hooks,JavaVM*vm,std::string*error){
 if(!redefinition_gate::Ready(vm)){
  if(error)*error="final-DEX-process-startup-or-pristine-gate-unproved";return false;
 }
 std::lock_guard<std::mutex>guard(installation);
 // Named hooks are retained for the process lifetime. Destructing this owner
 // at C++ static teardown would leave the live Direct trampoline dangling and
 // require JNI cleanup after the VM may already be gone. Retain the owner too.
 static ProcessFinalDex*process=nullptr;
 if(!process)process=new(std::nothrow) ProcessFinalDex;
 if(!process){if(error)*error="final-DEX-process-owner-allocation-failed";return false;}
 if(!process->observer.Install(hooks,vm,error))return false;
 published.store(process,std::memory_order_release);return true;
}
Provenance*BorrowFinalDexRecords(JavaVM*vm)noexcept{
 const auto*process=published.load(std::memory_order_acquire);
 return process?process->observer.Borrow(vm):nullptr;
}
void*FinalDex::Begin(void*owner,void*,void*,void*self,const void*definitions,int32_t)noexcept{
 auto&state=*static_cast<FinalDex*>(owner);
 if(state.pending_||state.deferred_)std::abort();
 if(!state.records_.Trusted()||!self||!state.current_||state.current_()!=self)return nullptr;
 if(redefinition_gate::Owned(state.vm_)){
  state.deferred_=state.records_.Deferred(self,definitions);
  if(state.deferred_)return state.deferred_;
 }
 JNIEnv*env=nullptr;
 if(!state.vm_||state.vm_->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!env||env->ExceptionCheck())return nullptr;
 try{
  std::vector<Definition>effective;
  // Capture ends every native mapping/view before the first Class/weak JNI
  // operation. The source vector still owns its borrowed Class handles.
  if(native_transformed_definitions::Capture(definitions,&effective,nullptr))
   state.pending_=state.records_.Prepare(env,effective);
 }catch(...){state.pending_.reset();}
 // Bookkeeping must not inject a new JNI exception into a public operation.
 // Entry excluded an existing exception; only our JNI allocation can set one.
 if(env->ExceptionCheck()){state.pending_.reset();env->ExceptionClear();}
 return state.pending_.get();
}
void FinalDex::End(void*owner,void*token,jvmtiError result,bool returned)noexcept{
 auto&state=*static_cast<FinalDex*>(owner);
 if(state.deferred_){
  if(token!=state.deferred_||state.records_.pending_!=state.deferred_||!redefinition_gate::Owned(state.vm_))std::abort();
  // The batch alone publishes after its extension returns and source owners
  // close. Direct success is not the outer batch completion receipt.
  if(!returned)state.records_.Invalidate();
  state.deferred_->direct_self_=nullptr;state.deferred_->direct_definitions_=nullptr;
  state.deferred_=nullptr;return;
 }
 if(token!=state.pending_.get())std::abort();
 if(returned&&result==JVMTI_ERROR_NONE&&state.pending_)state.pending_->Commit();
 else if(!returned||(result==JVMTI_ERROR_NONE&&!state.pending_))state.records_.Invalidate();
 state.pending_.reset();
}
}
