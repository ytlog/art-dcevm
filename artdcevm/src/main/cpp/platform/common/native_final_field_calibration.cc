#include "platform/common/native_final_field_calibration.h"
#include <cstdlib>
namespace artdcevm::final_field_calibration {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Roots {
 JNIEnv* env; jclass target=nullptr,writer=nullptr;jobject instance=nullptr,handles=nullptr;jclass exception=nullptr;
 ~Roots(){if(exception)env->DeleteLocalRef(exception);if(handles)env->DeleteGlobalRef(handles);if(instance)env->DeleteGlobalRef(instance);if(writer)env->DeleteGlobalRef(writer);if(target)env->DeleteGlobalRef(target);}
};
struct Probe {
 const Bindings* bindings;Roots* roots;jmethodID writer; jfieldID field;bool statik;
 final_write_guard::Oracle oracle;uint32_t original=0;bool modified=false,probing=false;
 std::string* error;
};
bool Install(const runnable_jni::Context&,void* raw){auto& p=*static_cast<Probe*>(raw);return p.bindings->service->InstallAtExclusiveBoundary(p.error);}
bool Observe(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Probe*>(raw);
 return p.bindings->service->FixtureField(context.self,p.roots->target,p.roots->writer,p.writer,p.field,p.statik,&p.oracle,&p.original,p.error)&&!(p.original&0x10u);
}
bool Arm(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Probe*>(raw);auto& service=*p.bindings->service;
 const auto expected=p.oracle;const auto flags=p.original;
 if(!service.FixtureField(context.self,p.roots->target,p.roots->writer,p.writer,p.field,p.statik,&p.oracle,&p.original,p.error)||
    p.oracle.field!=expected.field||p.oracle.method!=expected.method||p.original!=flags||(p.original&0x10u))return false;
 if(!service.SetFixtureFlags(p.oracle,p.original,p.original|0x10u,p.error))return false;
 p.modified=true;
 if(!service.BeginProof(p.oracle))return false;
 p.probing=true;return true;
}
bool Restore(const runnable_jni::Context& context,void* raw){auto& p=*static_cast<Probe*>(raw);
 final_write_guard::Oracle fresh;uint32_t flags=0;
 if(!p.bindings->service->FixtureField(context.self,p.roots->target,p.roots->writer,p.writer,p.field,p.statik,&fresh,&flags,p.error)||
    fresh.field!=p.oracle.field||fresh.method!=p.oracle.method||flags!=(p.original|0x10u)||
    !p.bindings->service->SetFixtureFlags(fresh,flags,p.original,p.error))return false;
 p.modified=false;return true;
}
bool ConsumeIllegalAccess(JNIEnv* env,jclass expected){jthrowable thrown=env->ExceptionOccurred();if(!thrown)return false;
 env->ExceptionClear();bool matches=env->IsInstanceOf(thrown,expected);if(!matches)env->Throw(thrown);env->DeleteLocalRef(thrown);return matches;
}
jint Read(JNIEnv* env,const Probe& p){return p.statik?env->GetStaticIntField(p.roots->target,p.field):env->GetIntField(p.roots->instance,p.field);}
void Write(JNIEnv* env,const Probe& p,jint value){if(p.statik)env->SetStaticIntField(p.roots->target,p.field,value);else env->SetIntField(p.roots->instance,p.field,value);}
// Restore is mandatory even with a pending original Java exception. Any failure
// to restore a modified fixture is a fatal source-lock/identity invariant.
void RestoreAlways(JNIEnv* env,Probe& p){jthrowable thrown=env->ExceptionOccurred();if(thrown)env->ExceptionClear();
 if(p.modified&&!p.bindings->exclusive_scope(p.bindings->owner,env,p.roots->instance,Restore,&p))std::abort();
 if(p.probing){p.bindings->service->EndProof(false,false);p.probing=false;}
 if(thrown){env->Throw(thrown);env->DeleteLocalRef(thrown);}
}
}
bool Run(JNIEnv* env,const Bindings& bindings,std::string* error){
 if(!env||env->ExceptionCheck()||!bindings.service||!bindings.hooks||!bindings.observed_instrumentation||
    !bindings.native_scope||!bindings.exclusive_scope)return Fail(error,"Final permission calibration lacks real source scope/receiver/service owner");
 Roots roots{env};
 jclass writer=env->FindClass("com/github/ytlog/artdcevm/internal/FinalPermissionOracle");
 if(!writer||env->ExceptionCheck())return Fail(error,"Installed final permission writer unavailable");
 jclass target=env->FindClass("com/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target");
 if(!target||env->ExceptionCheck()){env->DeleteLocalRef(writer);return Fail(error,"Installed final permission target unavailable");}
 jclass illegal=env->FindClass("java/lang/IllegalAccessError");
 if(!illegal||env->ExceptionCheck()){env->DeleteLocalRef(writer);env->DeleteLocalRef(target);return Fail(error,"Final permission exception Class unavailable");}
 roots.exception=illegal;
 roots.writer=static_cast<jclass>(env->NewGlobalRef(writer));roots.target=static_cast<jclass>(env->NewGlobalRef(target));
 env->DeleteLocalRef(writer);env->DeleteLocalRef(target);
 if(!roots.writer||!roots.target||env->ExceptionCheck())return false;
 auto method=[&](const char* name,const char* signature){return env->ExceptionCheck()?nullptr:env->GetStaticMethodID(roots.writer,name,signature);};
 jmethodID create=method("create","()Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;");
 jmethodID prepare=method("prepareHandles","()Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Handles;");
 jmethodID write=method("write","(Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;IZ)V");
 jmethodID check=method("checkHandles","(Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Handles;Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;ZI)I");
 jfieldID fields[2]={env->ExceptionCheck()?nullptr:env->GetFieldID(roots.target,"instanceValue","I"),env->ExceptionCheck()?nullptr:env->GetStaticFieldID(roots.target,"staticValue","I")};
 if(!roots.writer||!roots.target||!create||!prepare||!write||!check||!fields[0]||!fields[1]||env->ExceptionCheck())return Fail(error,"Final fixture public declarations incomplete");
 jobject object=env->CallStaticObjectMethod(roots.writer,create);
 if(!object||env->ExceptionCheck())return false;
 roots.instance=env->NewGlobalRef(object);env->DeleteLocalRef(object);
 if(!roots.instance||env->ExceptionCheck())return false;
 jobject handles=env->CallStaticObjectMethod(roots.writer,prepare);
 if(!handles||env->ExceptionCheck())return false;
 roots.handles=env->NewGlobalRef(handles);env->DeleteLocalRef(handles);
 if(!roots.instance||!roots.handles||env->ExceptionCheck())return false;
 // Resolve both actual writer field paths while mutable, before fixture-specific
 // breakpoint deoptimization. This probes current permissions of cached paths.
 for(jboolean statik:{JNI_FALSE,JNI_TRUE}){env->CallStaticVoidMethod(roots.writer,write,roots.instance,jint(41),statik);if(env->ExceptionCheck())return false;}
 auto& service=*bindings.service;
 if(!service.PrepareEnvironment(env,error)||!service.PrepareProofMethod(write,error)||
    !service.Bind(*bindings.hooks,bindings.observed_instrumentation,error))return false;
 Probe p{&bindings,&roots,write,fields[0],false,{},0,false,false,error};
 if(!bindings.exclusive_scope(bindings.owner,env,roots.instance,Install,&p))return false;
 // Populate the fixture interpreter cache after breakpoint/listener setup too;
 // JVMTI may invalidate the earlier compiled/interpreter cache during setup.
 for(jboolean statik:{JNI_FALSE,JNI_TRUE}){env->CallStaticVoidMethod(roots.writer,write,roots.instance,jint(41),statik);if(env->ExceptionCheck())return false;}
 bool complete=true;
 for(size_t i=0;i<2&&complete;++i){p.field=fields[i];p.statik=i!=0;
  const jint baseline=Read(env,p);
  if(env->ExceptionCheck()||!bindings.native_scope(bindings.owner,env,roots.instance,Observe,&p)||!bindings.exclusive_scope(bindings.owner,env,roots.instance,Arm,&p)){RestoreAlways(env,p);complete=false;break;}
  env->CallStaticVoidMethod(roots.writer,write,roots.instance,baseline+7,jboolean(p.statik));
  const bool rejected=ConsumeIllegalAccess(env,illegal);
  const bool unchanged=!env->ExceptionCheck()&&Read(env,p)==baseline&&!env->ExceptionCheck();
  const bool bytecode=service.EndProof(rejected,unchanged);p.probing=false;
  bool legal=false;
  if(bytecode&&!env->ExceptionCheck()){
   Write(env,p,baseline+11);
   const bool jni=!env->ExceptionCheck()&&Read(env,p)==baseline+11&&!env->ExceptionCheck();
   if(jni){const jint paths=env->CallStaticIntMethod(roots.writer,check,roots.handles,roots.instance,jboolean(p.statik),baseline+11);
    legal=!env->ExceptionCheck()&&paths==63&&Read(env,p)==baseline+12&&!env->ExceptionCheck();}
  }
  RestoreAlways(env,p);
  if(!env->ExceptionCheck())Write(env,p,baseline);
  complete=bytecode&&legal&&!env->ExceptionCheck();
 }
 return (complete&&service.CompleteCalibration())||Fail(error,"Final calibration requires actual cached bytecode event/rejection/unchanged store and legal JNI/reflection plus MH/VH getter/setter outcomes for both fields");
}
}
