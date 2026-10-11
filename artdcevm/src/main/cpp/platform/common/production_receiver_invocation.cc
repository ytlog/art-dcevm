#include "platform/common/production_receiver_invocation.h"
#include "platform/common/source_fixture_interpretation.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/redefinition_gate.h"
#include <array>
#include <cstdlib>
namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
using Journals=std::array<native_selective_interpretation::Service,10>;
struct RegisteredRoots {
 native_reference_decoder::Decoder&decoder;std::vector<jobject>roots;
 ~RegisteredRoots(){for(auto root:roots)decoder.Forget(root);}
 bool Add(JNIEnv*env,jobject root,std::string*error){
  // Allocate journal storage before registering, so C++ allocation refusal
  // cannot strand a decoder registration without its matching Forget.
  roots.push_back(root);
  if(decoder.RegisterStrong(env,root,error))return true;
  roots.pop_back();return false;
 }
};
struct Request {
 HookEngine&hooks;const MetadataCalibration&metadata;const source_handle_roots::Service&handles;
 const native_source_stop::Service&stop;receiver_guard::SourceCalibration&calibration;
 Journals&journals;std::string*error;bool completed=false;
};
bool Observe(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<Request*>(raw);const auto&inputs=r.metadata.Inputs();
 if(!actual.env||actual.env->ExceptionCheck()||!actual.original_leaf||
    actual.vm!=r.metadata.Runtime().receivers.java_vm||!runnable_jni::Bridge::BorrowOriginalScope(actual)||
    !redefinition_gate::Owned(static_cast<JavaVM*>(actual.vm)))
  return Fail(r.error,"receiver-invocation-exact-original-context-unavailable");
 source_fixture_roots::Owner roots(actual,r.handles,r.metadata.Fields());
 std::vector<jobject>classes;classes.reserve(inputs.ReceiverSamples().size());
 for(const auto&sample:inputs.ReceiverSamples())classes.push_back(sample.caller_class);
 std::vector<bounded_managed_roots::Slot>slots;
 if(!roots.Stage(classes,&slots,r.error)||slots.size()!=r.journals.size())return false;
 std::array<void*,10>callers{},targets{};
 const abi::common::RunnableJniScope scope{actual.self,actual.env,actual.vm};
 for(size_t n=0;n<r.journals.size();++n){
  const auto&sample=inputs.ReceiverSamples()[n];
  callers[n]=actual.original_leaf(scope,sample.caller.strong_global_method);
  if(!callers[n]||actual.env->ExceptionCheck())return false;
  targets[n]=actual.original_leaf(scope,sample.target.strong_global_method);
  if(!targets[n]||actual.env->ExceptionCheck())return false;
  const native_selective_interpretation::Method method{callers[n],slots[n],slots[n],
   sample.caller.dex_method_index,sample.caller.java_modifiers};
  if(!r.journals[n].Prepare(r.hooks,r.metadata.Methods(),r.metadata.Fields(),roots.Store(),&method,1,r.error)||
     !r.journals[n].PrepareRefund(r.hooks,r.error))return false;
 }
 // Public execution returns before refund. The unchanged SDK metadata stays
 // independently rooted; removing these owned native entries discards no Class
 // or method storage and does not certify a transaction-wide stack drain.
 for(size_t n=0;n<r.journals.size();++n){
  auto&journal=r.journals[n];bool activated=false,passed=false;
  try{
   activated=source_fixture_interpretation::Activate(r.stop,actual,roots,journal,r.error);
   if(activated){
    const auto&sample=inputs.ReceiverSamples()[n];const auto kind=static_cast<receiver_guard::InvokeKind>(n/2);
    const auto receiver=kind==receiver_guard::InvokeKind::Static?nullptr:inputs.ReceiverObject();
    const auto special=kind==receiver_guard::InvokeKind::Super?jobject(sample.caller_class):nullptr;
    if(r.calibration.Stage(actual.self,callers[n],targets[n],receiver,special,kind,n%2,
       sample.instructions.data(),sample.instructions.size())){
     // Once armed, always Finish even if public JNI preserves an exception.
     bool matched=false;const bool executed=inputs.InvokeReceiver(actual.env,n,&matched,r.error);
     passed=r.calibration.Finish(executed&&matched&&!actual.env->ExceptionCheck());
    }
   }
  }catch(...){
   // This same-thread public call has unwound. Finish also cancels any armed
   // sample before its ephemeral registered roots leave the episode.
   r.calibration.Finish(false);
   if(journal.PendingRefund())source_fixture_interpretation::Refund(r.stop,actual,roots,journal,r.error);
   throw;
  }
  if(activated||journal.PendingRefund()){
   if(!source_fixture_interpretation::Refund(r.stop,actual,roots,journal,r.error))return false;
  }
  if(!activated||!passed)return Fail(r.error,"receiver-invocation-original-event-or-public-result-not-proven");
 }
 r.completed=r.calibration.Seal()&&r.calibration.Proven()&&runnable_jni::Bridge::BorrowOriginalScope(actual)&&
  !actual.env->ExceptionCheck();
 return r.completed||Fail(r.error,"receiver-invocation-ten-original-forms-not-sealed");
}
}
bool CalibrateReceiverInvocations(HookEngine&hooks,JNIEnv*env,const MetadataCalibration&metadata,
 const source_handle_roots::Service&handles,const native_source_stop::Service&stop,
 native_reference_decoder::Decoder&decoder,receiver_guard::SourceCalibration&calibration,std::string*error){
 JavaVM*vm=nullptr;
 if(!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||vm!=metadata.Runtime().receivers.java_vm||
    !redefinition_gate::Owned(vm)||!handles.Calibrated()||!stop.Calibrated()||!decoder.Ready())
  return Fail(error,"receiver-invocation-ordinary-owner-or-source-dependencies-unavailable");
 if(calibration.Proven())return true;
 Journals journals;
 // Genuine ordinary Native calibration precedes the retained Runnable scope;
 // no nested executor or private Snapshot setter is used for these journals.
 for(auto&journal:journals)if(!journal.CalibrateOwner(hooks,env,error))return false;
 RegisteredRoots registered{decoder,{}};
 if(!registered.Add(env,metadata.Inputs().ReceiverObject(),error))return false;
 for(size_t n=2;n<4;++n)if(!registered.Add(env,metadata.Inputs().ReceiverSamples()[n].caller_class,error))return false;
 Request request{hooks,metadata,handles,stop,calibration,journals,error};
 const auto result=runnable_executor::WithOriginalScope(env,metadata.Inputs().Anchor(),Observe,&request);
 for(const auto&journal:journals)if(journal.PendingRefund())std::abort();
 return result==runnable_jni::Result::kSuccess&&request.completed&&calibration.Proven()&&!env->ExceptionCheck();
}
}
