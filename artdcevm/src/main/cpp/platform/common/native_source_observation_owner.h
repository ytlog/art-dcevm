#pragma once
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_hierarchy_session.h"
#include "platform/common/thread_suspend_service.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <mutex>
namespace artdcevm::native_source_observation {
// Process-owned callback hooks keep this owner alive. Installation establishes
// no calibration permission: each actual Run must still prove pause/cleanup.
class Owner final {
 public:
  bool Install(HookEngine& hooks,JNIEnv* env,const runtime_services::Snapshot& runtime,
               const thread_suspend_service::Service& state,void* self,size_t extent,
               std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    JavaVM* vm=nullptr;
    auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(
        hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    uint32_t value=UINT32_MAX;
    if(!env||!self||!current||current()!=self||env->GetJavaVM(&vm)!=JNI_OK||!vm||
       !runtime.runtime||runtime.receivers.java_vm!=vm||
       !state.State(self,extent,&value)||value!=abi::arm64::android11::kNative)
      return fail("Source observation installation requires actual same VM/Thread Native scope");
    if(bound_&&(vm_!=vm||runtime_!=runtime.runtime||!state_.SameBinding(state)))
      return fail("Source observation owner binding changed");
    if(!bound_){
      if(!native_source_class_loading::Service::Bind(hooks,runtime,&loading_,error))return false;
      vm_=vm;runtime_=runtime.runtime;state_=state;bound_=true;
    }
    if(installed_)return true;
    // The caller is already real JNI Native. Never simulate JNI start/end or
    // leave a suspend scope alive across a Java/native carrier return.
    bool result=false;
    {
      thread_suspend_service::SuspendScope stop(state_,"art-dcevm source observers");
      if(!stop.Supported()||current()!=self||!state_.State(self,extent,&value)||
         value!=abi::arm64::android11::kNative)
        return fail("Source observation installation lost Native owner");
      result=native_source_class_loading::Install(hooks,loading_,error)&&
             native_hierarchy_session::InstallPauseObservation(hooks,error);
    }
    if(current()!=self||!state_.State(self,extent,&value)||value!=abi::arm64::android11::kNative)
      return fail("Source observation installation returned on a different Native owner");
    installed_=result;return result;
  }
  native_source_class_loading::Service& Loading() noexcept{return loading_;}
 private:
  std::mutex mutex_;
  native_source_class_loading::Service loading_;
  thread_suspend_service::Service state_;
  JavaVM* vm_=nullptr;void* runtime_=nullptr;
  bool bound_=false,installed_=false;
};
}
