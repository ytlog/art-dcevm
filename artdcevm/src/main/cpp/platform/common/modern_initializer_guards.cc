#include "platform/common/modern_initializer_guards.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/common/jni_access.h"
#include "platform/abi/common/suspend_all.h"
#include <atomic>
#include <cstdarg>
#include <vector>
namespace artdcevm::retirement {
namespace initializer_entries {
using Scope=abi::common::RunnableJniScope;
using Decode=void*(*)(void*,jmethodID);
using Reflected=abi::common::FromReflectedMethod;
struct State {const RetiredMethodRegistry* registry=nullptr;void*manager=nullptr;
 jobject anchor=nullptr,probe_root=nullptr;bool probe_retired=false;void*(*current)()=nullptr;Decode decode=nullptr;
 Reflected reflected=nullptr;void*(*deliver)(void*)=nullptr;bool installed=false;};
State state;std::atomic<bool> calibrated{false};
void*original_check=nullptr,*original_constructor=nullptr,*original_obsolete=nullptr;
void*original_a[2]{},*original_v[2]{};
struct Trial {void*self=nullptr;uint32_t observed=0,delegated=0,rejected=0;void*expected=nullptr;bool identities=true;};
thread_local Trial* trial=nullptr;
void Observe(uint32_t path,void*self,void*method){if(trial&&trial->self==self){trial->observed|=path;trial->identities&=method==trial->expected;}}
struct Request {jmethodID id=nullptr;jobject mirror=nullptr;bool rejected=false;void*native=nullptr;uint32_t path=0;};
bool Native(const runnable_jni::Context& c,void*raw){auto&r=*static_cast<Request*>(raw);
 if(c.self!=state.current()||!MethodEntryGuardsOwn(*state.registry))return false;
 Scope scope{c.self,c.env,c.vm};r.native=r.mirror?state.reflected(scope,r.mirror):state.decode(state.manager,r.id);
 Observe(r.path,c.self,r.native);r.rejected=RejectRetiredMethod(r.native,c.self,nullptr);if(trial&&trial->self==c.self&&r.rejected)trial->rejected|=r.path;return true;
}
bool GuardNative(JNIEnv*env,jmethodID id,jobject mirror,uint32_t path){Request request{id,mirror,false,nullptr,path};
 // If the actual JNI/Runnable owner refuses, never proceed to allocation or an
 // unchecked old constructor. This is not a retirement permission.
 return runnable_executor::With(env,state.anchor,Native,&request)!=runnable_jni::Result::kSuccess||request.rejected;
}
bool Check(void*check,jmethodID id){void*self=state.current();void*method=state.decode(state.manager,id);Observe(1u,self,method);
 if(RejectRetiredMethod(method,self,nullptr)){if(trial&&trial->self==self)trial->rejected|=1u;return false;}if(trial&&trial->self==self)trial->delegated|=1u;
 using Fn=bool(*)(void*,jmethodID);return reinterpret_cast<Fn>(original_check)(check,id);
}
jobject Constructor(JNIEnv*env,jobject mirror,jobjectArray args){if(GuardNative(env,nullptr,mirror,2u))return nullptr;
 if(trial)trial->delegated|=2u;
 using Fn=jobject(*)(JNIEnv*,jobject,jobjectArray);return reinterpret_cast<Fn>(original_constructor)(env,mirror,args);
}
template<size_t indexed>jobject NewA(JNIEnv*env,jclass klass,jmethodID id,const jvalue*args){if(GuardNative(env,id,nullptr,4u<<indexed))return nullptr;
 if(trial)trial->delegated|=4u<<indexed;
 using Fn=jobject(*)(JNIEnv*,jclass,jmethodID,const jvalue*);return reinterpret_cast<Fn>(original_a[indexed])(env,klass,id,args);
}
template<size_t indexed>jobject NewV(JNIEnv*env,jclass klass,jmethodID id,va_list args){if(GuardNative(env,id,nullptr,16u<<indexed))return nullptr;
 if(trial)trial->delegated|=16u<<indexed;
 using Fn=jobject(*)(JNIEnv*,jclass,jmethodID,va_list);return reinterpret_cast<Fn>(original_v[indexed])(env,klass,id,args);
}
// The early fixed declaration does not return; newer source returns Context*. Both
// use the ARM64 pointer/word result register. Forward the actual named pending
// exception entry's result unchanged, never fabricate an unwind Context.
void* Obsolete(void*method,void*self){if(RejectRetiredMethod(method,self,nullptr))return state.deliver(self);
 using Fn=void*(*)(void*,void*);return reinterpret_cast<Fn>(original_obsolete)(method,self);
}
struct Identity {State*candidate;bool matched=false;};
bool Calibrate(const runnable_jni::Context& c,void*raw){auto&r=*static_cast<Identity*>(raw);r.matched=c.self&&c.self==r.candidate->current();return r.matched;}
bool Fail(std::string*error,const char*message){if(error)*error=message;return false;}
}
bool InstallInitializerEntryGuards(HookEngine&hooks,JNIEnv*env,jobject fixture,void*manager,RetiredMethodRegistry&registry,std::string*error){
 using namespace initializer_entries;
 if(state.registry)return state.installed&&state.registry==&registry&&state.manager==manager;
 if(!env||!fixture||!manager||!MethodEntryGuardsOwn(registry,manager)||!runnable_executor::Available())return Fail(error,"Initializer entries require the same installed persistent method owner");
 State candidate;candidate.registry=&registry;candidate.manager=manager;
 candidate.current=reinterpret_cast<void*(*)()>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
 candidate.decode=reinterpret_cast<Decode>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"));
 candidate.reflected=reinterpret_cast<Reflected>(hooks.FindSymbol("libart.so",abi::common::kFromReflectedMethod));
 candidate.deliver=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artDeliverPendingExceptionFromCode"));
 Identity identity{&candidate};if(!candidate.current||!candidate.decode||!candidate.reflected||!candidate.deliver||runnable_executor::With(env,fixture,Calibrate,&identity)!=runnable_jni::Result::kSuccess||!identity.matched)return Fail(error,"Initializer named leaves/current source identity unavailable");
 struct Entry{const char*symbol;void*proxy;void**original;void*target=nullptr;unsigned family=0;};
 Entry entries[]{
 {"_ZN3art12_GLOBAL__N_111ScopedCheck16CheckConstructorEP10_jmethodID",reinterpret_cast<void*>(Check),&original_check},
 {"_ZN3artL24Constructor_newInstance0EP7_JNIEnvP8_jobjectP13_jobjectArray",reinterpret_cast<void*>(Constructor),&original_constructor},
 {"_ZN3art3JNIILb0EE10NewObjectAEP7_JNIEnvP7_jclassP10_jmethodIDPK6jvalue",reinterpret_cast<void*>(NewA<0>),&original_a[0],nullptr,1},
 {"_ZN3art3JNIILb1EE10NewObjectAEP7_JNIEnvP7_jclassP10_jmethodIDPK6jvalue",reinterpret_cast<void*>(NewA<1>),&original_a[1],nullptr,1},
 {"_ZN3art3JNIILb0EE10NewObjectVEP7_JNIEnvP7_jclassP10_jmethodIDSt9__va_list",reinterpret_cast<void*>(NewV<0>),&original_v[0],nullptr,2},
 {"_ZN3art3JNIILb1EE10NewObjectVEP7_JNIEnvP7_jclassP10_jmethodIDSt9__va_list",reinterpret_cast<void*>(NewV<1>),&original_v[1],nullptr,2},
 {"artInvokeObsoleteMethod",reinterpret_cast<void*>(Obsolete),&original_obsolete}};
 for(size_t i=0;i<7;++i){auto&entry=entries[i];entry.target=hooks.FindSymbol("libart.so",entry.symbol);if(!entry.target){if(error)*error=std::string("Missing initializer entry: ")+entry.symbol;return false;}
  for(size_t j=0;j<i;++j)if(entries[j].target==entry.target&&(!entry.family||entry.family!=entries[j].family))return Fail(error,"Initializer symbols alias incompatible fixed source declarations");}
 using namespace abi::common;auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!construct||!destroy)return Fail(error,"Initializer installation lacks original exclusive owner");
 candidate.anchor=env->NewGlobalRef(fixture);if(!candidate.anchor||env->ExceptionCheck())return false;
 struct Root {JNIEnv*env;jobject value;~Root(){if(state.anchor!=value)env->DeleteGlobalRef(value);}} root{env,candidate.anchor};
 SuspendAll stopped{};construct(&stopped,"artdcevm initializer entry installation",false);
 struct Stop{SuspendAll*value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
 state=candidate;std::vector<void*> tokens;tokens.reserve(7);
 for(size_t n=0;n<7;++n){auto&entry=entries[n];bool alias=false;for(size_t j=0;j<n;++j)if(entries[j].target==entry.target){*entry.original=*entries[j].original;alias=true;break;}
  // Linker aliases of the two identical-ABI JNI template declarations share a
  // single real hook/trampoline. Never install two observers on one target.
  if(alias)continue;
  void*token=hooks.Install(entry.target,entry.proxy,entry.original,error);if(!token){bool removed=true;for(auto i=tokens.rbegin();i!=tokens.rend();++i){std::string rollback;if(!hooks.Remove(*i,&rollback)){removed=false;if(error)*error+="; "+rollback;}}if(removed)state={};return false;}tokens.push_back(token);}
 state.installed=true;return true;
}
namespace initializer_entries {
struct Producer{jmethodID id;jobject mirror;Trial*trial;};
bool PrepareTrial(const runnable_jni::Context&c,void*raw){auto&p=*static_cast<Producer*>(raw);if(c.self!=state.current())return false;Scope scope{c.self,c.env,c.vm};p.trial->self=c.self;p.trial->expected=state.decode(state.manager,p.id);return p.trial->expected&&state.reflected(scope,p.mirror)==p.trial->expected;}
jobject PublicNewV(JNIEnv*env,jclass klass,jmethodID id,...){va_list args;va_start(args,id);jobject result=env->NewObjectV(klass,id,args);va_end(args);return result;}
}
bool CalibrateInitializerEntryGuards(JNIEnv*env,RetiredMethodRegistry&registry,std::string*error){using namespace initializer_entries;
 if(!state.installed||state.registry!=&registry||!MethodEntryGuardsOwn(registry)||!env||env->ExceptionCheck())return Fail(error,"Initializer public producer lacks exact installed owner");
 if(calibrated.load(std::memory_order_acquire))return true;
 if(state.probe_retired)return Fail(error,"Private initializer oracle was retired before its complete negative calibration");
 jclass klass=env->FindClass("com/github/ytlog/artdcevm/internal/InitializerGuardOracle");if(!klass||env->ExceptionCheck())return false;
 struct Locals{JNIEnv*env;jobject refs[5]{};~Locals(){for(auto ref:refs)if(ref)env->DeleteLocalRef(ref);}} locals{env,{klass}};
 jmethodID id=env->GetMethodID(klass,"<init>","()V");jfieldID field=env->GetFieldID(klass,"intValue","I"),counter=env->GetStaticFieldID(klass,"constructorCalls","I");if(!id||!field||!counter||env->ExceptionCheck())return false;
 const jint before_calls=env->GetStaticIntField(klass,counter);if(env->ExceptionCheck())return false;
 jobject mirror=env->ToReflectedMethod(klass,id,false);locals.refs[1]=mirror;if(!mirror||env->ExceptionCheck())return false;
 jclass constructor=env->GetObjectClass(mirror);locals.refs[2]=constructor;auto accessible=constructor?env->GetMethodID(constructor,"setAccessible","(Z)V"):nullptr;auto new_instance=constructor?env->GetMethodID(constructor,"newInstance","([Ljava/lang/Object;)Ljava/lang/Object;"):nullptr;
 if(!accessible||!new_instance||env->ExceptionCheck())return false;env->CallVoidMethod(mirror,accessible,JNI_TRUE);if(env->ExceptionCheck())return false;
 jclass object=env->FindClass("java/lang/Object");locals.refs[3]=object;auto empty=object?env->NewObjectArray(0,object,nullptr):nullptr;locals.refs[4]=empty;if(!empty||env->ExceptionCheck())return false;
 Trial observed;Producer producer{id,mirror,&observed};if(runnable_executor::With(env,state.anchor,PrepareTrial,&producer)!=runnable_jni::Result::kSuccess)return Fail(error,"Independent JNI ID/reflection source constructor identity differs");
 if(trial)return Fail(error,"Initializer producer cannot nest");trial=&observed;struct Restore{~Restore(){trial=nullptr;}} restore;
 for(unsigned path=0;path<3;++path){jobject result=path==0?env->NewObjectA(klass,id,nullptr):path==1?PublicNewV(env,klass,id):env->CallObjectMethod(mirror,new_instance,empty);
  bool pass=result&&!env->ExceptionCheck();jclass result_class=pass?env->GetObjectClass(result):nullptr;pass=pass&&result_class&&env->IsSameObject(result_class,klass)&&env->GetIntField(result,field)==static_cast<jint>(0x81234567u)&&!env->ExceptionCheck();if(result_class)env->DeleteLocalRef(result_class);if(result)env->DeleteLocalRef(result);if(!pass)return Fail(error,"Actual public constructor result/body differs from independent fixture");}
 if(!observed.identities||!(observed.observed&2u)||!(observed.observed&12u)||!(observed.observed&48u))return Fail(error,"Actual reflection and both JNI allocation entry observations incomplete");
 if(env->GetStaticIntField(klass,counter)!=before_calls+3||env->ExceptionCheck())return Fail(error,"Original private constructor bodies were not independently observed");
 // Publish a real retirement in the same persistent registry, never a trial
 // Boolean. This private one-shot oracle is never used by application code.
 state.probe_root=env->NewGlobalRef(mirror);if(!state.probe_root||env->ExceptionCheck())return false;
 registry.Retire(observed.expected);state.probe_retired=true;
 const uint32_t positive_mask=observed.observed;observed.observed=observed.delegated=observed.rejected=0;
 jclass missing=env->FindClass("java/lang/NoSuchMethodError");if(!missing||env->ExceptionCheck())return false;
 struct ExceptionType{JNIEnv*env;jclass type;~ExceptionType(){env->DeleteLocalRef(type);}} exception_type{env,missing};
 for(unsigned path=0;path<3;++path){observed.rejected=0;jobject result=path==0?env->NewObjectA(klass,id,nullptr):path==1?PublicNewV(env,klass,id):env->CallObjectMethod(mirror,new_instance,empty);
  const uint32_t required=path==2?2u:1u|(positive_mask&(path==0?12u:48u));
  jthrowable exception=env->ExceptionOccurred();env->ExceptionClear();bool pass=!result&&exception&&env->IsInstanceOf(exception,missing);if(result)env->DeleteLocalRef(result);if(exception)env->DeleteLocalRef(exception);if(!pass||!(observed.rejected&required)||env->ExceptionCheck())return Fail(error,"Actual retired private constructor did not reject with NoSuchMethodError at its original allocation boundary");}
 if(observed.delegated||!observed.identities||env->GetStaticIntField(klass,counter)!=before_calls+3||env->ExceptionCheck())return Fail(error,"Retired constructor reached original allocation/body or lost an actual entry observation");
 calibrated.store(true,std::memory_order_release);if(error)error->clear();return true;
}
bool InitializerEntryGuardsOwn(const RetiredMethodRegistry&registry){using namespace initializer_entries;return state.installed&&state.registry==&registry&&MethodEntryGuardsOwn(registry)&&calibrated.load(std::memory_order_acquire);}
}
