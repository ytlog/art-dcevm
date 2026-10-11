#include "platform/common/stack_visitor_calibration.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/redefinition_gate.h"
#include "platform/abi/arm64/common/context.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <memory>
namespace artdcevm::stack_visitor_calibration {
namespace {
using Declaration=stack_visitor_service::Declaration;
bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}
// Native C2 writes one complete supported fixed declaration. Keep all scalar
// callback state outside its maximum storage and restore this actual vptr via
// Service::Construct. No candidate WalkStack is ever invoked by this probe.
struct Probe final:abi::arm64::stack::Visitor12 {
 bool VisitFrame()override{return false;}
};
static_assert(sizeof(Probe)>=sizeof(abi::arm64::stack::Visitor11)&&
 sizeof(Probe)>=sizeof(abi::arm64::stack::Visitor13)&&sizeof(Probe)>=sizeof(abi::arm64::stack::Visitor17));
struct Request {
 stack_visitor_service::Service stack;
 abi::arm64::android_common::CreateContext create;
 abi::arm64::android11::CurrentThread current;
 JavaVM*vm;method_identity::Model model;bool walk17;
 Declaration observed=Declaration::kAndroid12;
 bool completed=false;std::string*error;
};
bool Observe(const runnable_jni::Context&actual,void*raw){
 auto&r=*static_cast<Request*>(raw);
 if(!runnable_jni::Bridge::BorrowOriginalScope(actual)||actual.vm!=r.vm||!actual.env||
    actual.env->ExceptionCheck()||!redefinition_gate::Owned(r.vm)||r.current()!=actual.self)
  return Fail(r.error,"stack-constructor-calibration-original-context-unavailable");
 // Created and destroyed solely through ART's named factory/native virtual D0.
 // No Context tail, register write, stack resume or long jump is exercised.
 std::unique_ptr<abi::arm64::android_common::ContextPrefix>context(r.create());
 if(!context)return Fail(r.error,"stack-constructor-calibration-native-Context-unavailable");
 Probe probe{};
 if(!r.stack.Construct(&probe,sizeof(probe),actual.self,context.get(),
      stack_visitor_service::WalkKind::kIncludeInlined,false)||
    probe.thread!=actual.self||probe.kind!=stack_visitor_service::WalkKind::kIncludeInlined)
  return Fail(r.error,"stack-constructor-calibration-current-prefix-mismatch");
 const auto mask=abi::arm64::stack::ConstructorContextEchoMask(&probe,sizeof(probe),context.get());
 // 11 and 17 share their Context offset. The unique named Walk template
 // declaration separates them; any extra/absent echo refuses publication.
 if(r.walk17){if(mask!=9u)return Fail(r.error,"stack-constructor-calibration-17-storage-mismatch");r.observed=Declaration::kAndroid17;}
 else if(mask==9u)r.observed=Declaration::kAndroid11;
 else if(mask==2u)r.observed=Declaration::kAndroid12;
 else if(mask==4u)r.observed=Declaration::kAndroid13To16;
 else return Fail(r.error,"stack-constructor-calibration-storage-ambiguous");
 if((r.observed==Declaration::kAndroid11)!=(r.model==method_identity::Model::kMethod11))
  return Fail(r.error,"stack-constructor-calibration-independent-method-model-mismatch");
 r.completed=r.current()==actual.self&&runnable_jni::Bridge::BorrowOriginalScope(actual)&&!actual.env->ExceptionCheck();
 return r.completed;
}
}
bool Calibrate(HookEngine&hooks,JNIEnv*env,jobject root,const method_identity::Calibration&methods,
               Declaration*out,std::string*error){
 JavaVM*vm=nullptr;
 if(!out||!env||!root||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||!vm||
    !redefinition_gate::Owned(vm)||methods.identities.empty()||
    (methods.model!=method_identity::Model::kMethod11&&methods.model!=method_identity::Model::kMethod12Onward)||
    env->GetObjectRefType(root)!=JNIGlobalRefType||env->ExceptionCheck())
  return Fail(error,"stack-constructor-calibration-owned-inputs-unavailable");
 auto finder=[&](const char*name){return hooks.FindSymbol("libart.so",name);};
 const bool legacy=finder(abi::arm64::stack::kWalk)!=nullptr;
 const bool modern=finder(abi::arm64::stack::kWalk17)!=nullptr;
 if(legacy==modern)return Fail(error,"stack-constructor-calibration-walk-declaration-ambiguous-or-absent");
 auto create=reinterpret_cast<abi::arm64::android_common::CreateContext>(finder(abi::arm64::android_common::kCreateContext));
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(finder(abi::arm64::android11::kCurrentThread));
 Request request{{},create,current,vm,methods.model,modern,Declaration::kAndroid12,false,error};
 // Binding here only reserves the maximum fixed constructor storage and exact
 // named function signatures. The observed tail declaration is published later.
 if(!create||!current||!stack_visitor_service::Service::Bind(modern?Declaration::kAndroid17:Declaration::kAndroid12,finder,&request.stack))
  return Fail(error,"stack-constructor-calibration-named-operations-unavailable");
 if(runnable_executor::WithOriginalScope(env,root,Observe,&request)!=runnable_jni::Result::kSuccess||
    env->ExceptionCheck()||!request.completed)return false;
 *out=request.observed;return true;
}
}
