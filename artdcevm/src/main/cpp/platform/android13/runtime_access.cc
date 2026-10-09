#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/common/runtime_services.h"
#include <cstdlib>
#include "platform/android13/native_object_access.h"
namespace artdcevm::android13 {
namespace {
void** runtime_instance = nullptr;
void* calibrated_instrumentation=nullptr;
void* calibrated_runtime=nullptr;
RuntimeReceivers calibrated_receivers;
OwnedJitReceivers calibrated_jit;
using DecodeMethod = void* (*)(void*, jmethodID);
using DecodeField = void* (*)(void*, jfieldID);
DecodeMethod decode_method = nullptr;
DecodeField decode_field = nullptr;
bool fields_proven=false;
runtime_services::View Services(void* runtime) {
  runtime_services::View result;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid13,runtime,
      sizeof(runtime_access_abi::RuntimePrefix),&result)) std::abort();
  // These are original Runtime-lifetime services. Reading a replacement is not
  // calibration: never delegate a private function to an unobserved receiver.
  if(result.class_linker!=calibrated_receivers.class_linker ||
     result.jni_id_manager!=calibrated_receivers.jni_id_manager ||
     result.java_vm!=calibrated_receivers.java_vm ||
     result.thread_list!=calibrated_receivers.thread_list ||
     result.heap!=calibrated_jit.heap ||
     (result.jit && (result.jit!=calibrated_jit.jit || result.jit_code_cache!=calibrated_jit.code_cache)))
    std::abort();
  return result;
}
}
void* RuntimeInstance() {
  if(!runtime_instance || *runtime_instance!=calibrated_runtime)std::abort();
  return calibrated_runtime;
}
void* RuntimeInstrumentation() {
  (void)RuntimeInstance();
  return calibrated_instrumentation;
}
void* RuntimeClassLinker() { return Services(RuntimeInstance()).class_linker; }
void* RuntimeJniIdManager() { return Services(RuntimeInstance()).jni_id_manager; }
void* RuntimeJit() { return Services(RuntimeInstance()).jit; }
void* RuntimeJitCodeCache() {
  return Services(RuntimeInstance()).jit_code_cache;
}
void* DecodeMethodId(jmethodID id) {
  return (reinterpret_cast<uintptr_t>(id) & 1) ? decode_method(RuntimeJniIdManager(), id) :
      reinterpret_cast<void*>(id);
}
void* DecodeFieldId(jfieldID id) {
  return id&&fields_proven?decode_field(RuntimeJniIdManager(),id):nullptr;
}
namespace {
struct OwnedGlobal {JNIEnv* env;jobject value;~OwnedGlobal(){if(value)env->DeleteGlobalRef(value);}};
struct PublicText {jvmtiEnv* ti;char* value=nullptr;~PublicText(){if(value)ti->Deallocate(reinterpret_cast<unsigned char*>(value));}};
bool ProveFields(JNIEnv* env,jvmtiEnv* ti,jclass klass,void* (*leaf)(void*),size_t* seen,std::string* error,bool fixture_lookup){
 if(!klass||env->ExceptionCheck())return false;
 OwnedGlobal owner{env,env->NewGlobalRef(klass)};
 if(!owner.value||env->ExceptionCheck())return false;
 jint count=0;jfieldID* ids=nullptr;
 struct FieldIds {jvmtiEnv* ti;jfieldID*& ids;~FieldIds(){if(ids)ti->Deallocate(reinterpret_cast<unsigned char*>(ids));}} allocation{ti,ids};
 if(ti->GetClassFields(klass,&count,&ids)!=JVMTI_ERROR_NONE||count<0||(count&&!ids))return false;
 for(jint i=0;i<count;++i){
  PublicText name{ti},descriptor{ti};jint flags=0;jclass declaring=nullptr;
  if(!ids[i]||ti->GetFieldName(klass,ids[i],&name.value,&descriptor.value,nullptr)!=JVMTI_ERROR_NONE||
     ti->GetFieldModifiers(klass,ids[i],&flags)!=JVMTI_ERROR_NONE)return false;
  auto owner_result=ti->GetFieldDeclaringClass(klass,ids[i],&declaring);
  struct Local {JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}} declared{env,declaring};
  if(owner_result!=JVMTI_ERROR_NONE||!name.value||!descriptor.value||!declaring||!env->IsSameObject(declaring,klass)||env->ExceptionCheck())return false;
  bool stat=(flags&8)!=0;
  // Only the owned fixture declarations promise public name lookup. Ancestors
  // can include ART shadow fields exposed by JVMTI but hidden by FindFieldID.
  // Original ToReflectedField accepts those IDs directly and constructs their
  // actual Field mirror; retain the full mirror/ID/native proof below.
  if(fixture_lookup){
   jfieldID looked=stat?env->GetStaticFieldID(klass,name.value,descriptor.value):env->GetFieldID(klass,name.value,descriptor.value);
   if(!looked||looked!=ids[i]||env->ExceptionCheck())return false;
  }
  Local reflected{env,env->ToReflectedField(klass,ids[i],stat)};
  if(!reflected.value||env->ExceptionCheck()||env->FromReflectedField(reflected.value)!=ids[i]||env->ExceptionCheck())return false;
  OwnedGlobal mirror{env,env->NewGlobalRef(reflected.value)};
  if(!mirror.value||env->ExceptionCheck())return false;
  NativeObjectAccess access(env);if(!access.Supported())return false;
  // The manager may synchronize/suspend. Both public Class and Field roots are
  // owned before entering it; obtain the mirror pointer only AFTER it returns.
  void* native=decode_field(RuntimeJniIdManager(),ids[i]);
  void* actual=leaf(access.Decode(mirror.value));
  if(!native||native!=actual){if(error)*error="Legacy public field ID differs from independently rooted named Field identity";return false;}
  ++*seen;
 }
 // Each recursion uses public JNI roots; no Class pointer crosses a JNI call.
 jclass parent=env->GetSuperclass(klass);
 if(env->ExceptionCheck())return false;
 bool result=!parent||ProveFields(env,ti,parent,leaf,seen,error,false);
 if(parent)env->DeleteLocalRef(parent);return result;
}
}
bool CalibrateRuntimeFieldIds(HookEngine& hooks,JNIEnv* env,jvmtiEnv* ti,jobject primary,jobject storage,std::string* error){
 if(fields_proven)return true;
 if(!env||!ti||!primary||!storage||!decode_field||env->ExceptionCheck())return false;
 JavaVM* vm=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||vm!=calibrated_receivers.java_vm||env->ExceptionCheck())return false;
 auto leaf=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 if(!leaf){if(error)*error="Legacy field IDs require original named reflected Field identity";return false;}
 jclass a=env->GetObjectClass(primary),b=env->GetObjectClass(storage);
 struct Classes {JNIEnv* env;jclass a,b;~Classes(){if(a)env->DeleteLocalRef(a);if(b)env->DeleteLocalRef(b);}} roots{env,a,b};
 if(!a||!b||env->ExceptionCheck()||env->IsSameObject(a,b))return false;
 size_t first=0,second=0;
 if(!ProveFields(env,ti,a,leaf,&first,error,true)||!ProveFields(env,ti,b,leaf,&second,error,true)||!first||!second||env->ExceptionCheck())return false;
 fields_proven=true;return true;
}
bool ConfigureRuntimeAccess(HookEngine& hooks, const DeviceProfile& profile, std::string* error,
                            const RuntimeReceivers* receivers,const OwnedJitReceivers* jit_receivers) {
  if (!profile.Matched()) { *error = "Runtime access requires a matched device profile"; return false; }
  if(!receivers || !receivers->class_linker || !receivers->jni_id_manager || !receivers->java_vm || !receivers->thread_list || !receivers->instrumentation) {
    *error="Runtime access requires independently observed public JNI service receivers";return false;
  }
  if(!jit_receivers || !jit_receivers->jit || !jit_receivers->code_cache || !jit_receivers->heap) {
    *error="Runtime access requires independently observed JIT/cache/heap receivers";return false;
  }
  auto instance = reinterpret_cast<void**>(hooks.FindSymbol("libart.so", runtime_access_abi::kRuntimeInstance));
  auto method = reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeMethodId));
  auto field = reinterpret_cast<DecodeField>(hooks.FindSymbol("libart.so", runtime_access_abi::kDecodeFieldId));
  if (!instance || !*instance || !method || !field) {
    *error = "Missing named Runtime singleton, JNI ID decoders";
    return false;
  }
  runtime_services::View services;
  if(!runtime_services::Read(runtime_services::Declaration::kAndroid13,*instance,
      sizeof(runtime_access_abi::RuntimePrefix),&services)) {
    *error="Invalid calibrated Runtime service storage";return false;
  }
  if (!services.class_linker || !services.jni_id_manager) {
    *error = "Missing fixed Runtime ClassLinker or JNI ID manager";
    return false;
  }
  if(services.class_linker!=receivers->class_linker ||
     services.jni_id_manager!=receivers->jni_id_manager || services.java_vm!=receivers->java_vm ||
     services.thread_list!=receivers->thread_list) {
    *error="Runtime service storage disagrees with public JNI receiver observations";return false;
  }
  void* instrumentation=static_cast<unsigned char*>(*instance)+
      offsetof(selective_abi::RuntimePrefix,instrumentation);
  if(instrumentation!=receivers->instrumentation) {
    *error="Runtime Instrumentation storage disagrees with owned JVMTI listener receiver";return false;
  }
  if(services.jit!=jit_receivers->jit || services.jit_code_cache!=jit_receivers->code_cache ||
     services.heap!=jit_receivers->heap) {
    *error="Runtime JIT/cache/heap storage disagrees with owned ART receiver observations";return false;
  }
  calibrated_runtime=*instance;
  calibrated_receivers=*receivers;
  calibrated_jit=*jit_receivers;
  calibrated_instrumentation=instrumentation;
  runtime_instance = instance;
  decode_method = method;
  decode_field = field;
  fields_proven=false;
  return true;
}
}
