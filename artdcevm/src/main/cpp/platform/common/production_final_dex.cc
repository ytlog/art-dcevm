#include "platform/common/production_final_dex.h"
#include "platform/common/native_transformed_definitions.h"

namespace artdcevm::production_bootstrap {
bool FinalDex::Install(HookEngine&hooks,JavaVM*vm,std::string*error){
 const auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(!vm||(vm_&&vm_!=vm)||!records_.Trusted())return fail("final-DEX-observer-VM-or-source-invalid");
 if(!current_){
  if(!hooks.Initialize(error))return false;
  current_=reinterpret_cast<abi::common::CurrentThread>(hooks.FindSymbol("libart.so",abi::common::kCurrentThread));
  if(!current_)return fail("final-DEX-observer-original-current-thread-unavailable");
 }
 vm_=vm;
 if(!redefinition_gate::InstallDirectObservation(hooks,&observer_,error))return false;
 redefinition_gate::Lease lease(vm);
 if(!lease.Acquired())return fail("final-DEX-observer-history-lease-unavailable");
 if(!redefinition_gate::ObservationComplete(vm,&observer_)){
  records_.Invalidate();return fail("final-DEX-observer-missed-public-source-history");
 }
 return true;
}
void*FinalDex::Begin(void*owner,void*,void*,void*self,const void*definitions,int32_t)noexcept{
 auto&state=*static_cast<FinalDex*>(owner);
 if(state.pending_)std::abort();
 if(!state.records_.Trusted()||!self||!state.current_||state.current_()!=self)return nullptr;
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
 if(token!=state.pending_.get())std::abort();
 if(returned&&result==JVMTI_ERROR_NONE&&state.pending_)state.pending_->Commit();
 else if(!returned||(result==JVMTI_ERROR_NONE&&!state.pending_))state.records_.Invalidate();
 state.pending_.reset();
}
}
