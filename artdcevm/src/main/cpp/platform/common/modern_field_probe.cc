// Debug-only public-reflection oracle for the production metadata service.
#ifdef ARTDCEVM_TESTING
#include "platform/common/modern_field_metadata.h"
#include "platform/common/field_mirror_calibration.h"
#include "platform/common/modern_field_storage.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/field_storage.h"
#include "hooks/shadow_hook_engine.h"
#include <vector>
#include <stdexcept>
namespace {
using namespace artdcevm;
thread_local size_t calibrated_card_offset=0;
struct Roots {
  JNIEnv* env;std::vector<jobject> values;
  ~Roots(){for(auto value:values)env->DeleteGlobalRef(value);}
  jobject Own(jobject value){auto root=env->NewGlobalRef(value);if(root)values.push_back(root);return root;}
};
struct Request {
  modern_field_metadata::FieldMetadata metadata;
  jobject owner;
  const std::vector<modern_field_metadata::FieldOracle>* fields;
  std::string descriptor,error;
  field_mirror::NamedField named_field=nullptr;
  field_mirror::Calibration mirror;
};
bool Observe(const runnable_jni::Context& scope,void* data) {
  auto& request=*static_cast<Request*>(data);
  return request.metadata.Calibrate(scope,request.owner,request.descriptor.c_str(),
      request.fields->data(),request.fields->size(),&request.error)&&
      field_mirror::Calibrate(scope,request.metadata,request.named_field,request.owner,
      request.fields->data(),request.fields->size(),&request.mirror,&request.error);
}
std::string String(JNIEnv* env,jstring value) {
  if(!value)return {};
  const char* bytes=env->GetStringUTFChars(value,nullptr);
  if(!bytes)return {};
  std::string text(bytes);env->ReleaseStringUTFChars(value,bytes);return text;
}
bool Transfer(const exclusive_owned_roots::Context& scope,void*) {
  if(scope.count!=2)return false;
  auto* source=static_cast<unsigned char*>(scope.roots[0]);
  auto* target=static_cast<unsigned char*>(scope.roots[1]);
  uint32_t source_word=0,target_word=0;
  std::memcpy(&source_word,source+4,4);std::memcpy(&target_word,target+4,4);
  field_storage::TransferMonitor(target,source,4,0);
  uint32_t moved=0,cleared=1;
  std::memcpy(&moved,target+4,4);std::memcpy(&cleared,source+4,4);
  // Restore the source hash and each collector's original object bits before
  // releasing the stop; retain the transferred hash on the target.
  constexpr uint32_t gc_mask=0x30000000u;
  const uint32_t target_hash=(source_word&~gc_mask)|(target_word&gc_mask);
  std::memcpy(source+4,&source_word,4);std::memcpy(target+4,&target_hash,4);
  return moved==source_word&&cleared==0;
}
bool Reject(const exclusive_owned_roots::Context&,void*){return false;}
bool Throw(const exclusive_owned_roots::Context&,void*){throw std::runtime_error("owned stop recovery oracle");}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeFields(
    JNIEnv* env,jclass,jclass owner,jstring descriptor,jobjectArray fields,
    jobjectArray names,jobjectArray types,jintArray modifiers,jboolean storage_required) {
  auto failure=[&](const std::string& error){return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());};
  if(!owner||!descriptor||!fields||!names||!types||!modifiers||env->ExceptionCheck())return failure("invalid field oracle");
  const jsize count=env->GetArrayLength(fields);
  if(count<2||count>1024||env->GetArrayLength(names)!=count||env->GetArrayLength(types)!=count||env->GetArrayLength(modifiers)!=count)
    return failure("invalid field oracle dimensions");
  Roots roots{env,{}};jobject global_owner=roots.Own(owner);
  if(!global_owner)return failure("cannot own fixture class");
  std::vector<jint> flags(count);env->GetIntArrayRegion(modifiers,0,count,flags.data());
  std::vector<std::string> stable_names(count),stable_types(count);
  std::vector<modern_field_metadata::FieldOracle> oracles;
  for(jsize i=0;i<count;++i) {
    jobject field=env->GetObjectArrayElement(fields,i);
    auto name=static_cast<jstring>(env->GetObjectArrayElement(names,i));
    auto type=static_cast<jstring>(env->GetObjectArrayElement(types,i));
    if(!field||!name||!type||env->ExceptionCheck())return failure("missing public field oracle");
    jobject global=roots.Own(field);
    stable_names[i]=String(env,name);stable_types[i]=String(env,type);
    env->DeleteLocalRef(field);env->DeleteLocalRef(name);env->DeleteLocalRef(type);
    if(!global||env->ExceptionCheck())return failure("cannot own reflected field");
    oracles.push_back({global,stable_names[i].c_str(),stable_types[i].c_str(),static_cast<uint32_t>(flags[i])});
  }
  Request request{{},global_owner,&oracles,String(env,descriptor),{},nullptr,{}};
  ShadowHookEngine hooks;
  if(!hooks.Initialize(&request.error)||!runnable_executor::Initialize(hooks,env,&request.error)||
      !request.metadata.Bind(hooks,&request.error))return failure(request.error);
  request.named_field=reinterpret_cast<field_mirror::NamedField>(
      hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
  auto result=runnable_executor::With(env,global_owner,Observe,&request);
  if(result!=runnable_jni::Result::kSuccess)return failure("field metadata: "+request.error);
  if(storage_required) {
  jmethodID constructor=env->GetMethodID(owner,"<init>","()V");
  jobject fixture=constructor?env->NewObject(owner,constructor):nullptr;
  if(!fixture||env->ExceptionCheck())return failure("owned storage fixture construction failed");
  jobject global_fixture=roots.Own(fixture);env->DeleteLocalRef(fixture);
  modern_field_storage::Service storage;
  if(!storage.Calibrate(hooks,env,global_owner,global_fixture,request.metadata,
      oracles.data(),oracles.size(),&request.error))return failure("storage: "+request.error);
  calibrated_card_offset=storage.CardTableOffset();
  auto rejected=oracles;rejected[0].name="__not_a_fixture_field__";
  std::string rejected_error;
  if(storage.Calibrate(hooks,env,global_owner,global_fixture,request.metadata,
      rejected.data(),rejected.size(),&rejected_error)||!storage.Calibrated()||env->ExceptionCheck())
    return failure("storage rejection did not preserve calibrated service");
  }
  return failure(request.metadata.StorageModel()==modern_field_metadata::Model::kMerged?"merged":"split");
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeCardOffset(JNIEnv*,jclass){return static_cast<jint>(calibrated_card_offset);}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeExclusive(JNIEnv* env,jclass,jclass owner,jint iterations) {
  auto failure=[&](const std::string& error){return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());};
  if(!owner||iterations<1||iterations>128||env->ExceptionCheck())return failure("invalid exclusive oracle");
  ShadowHookEngine hooks;exclusive_owned_roots::Service service;std::string error;
  if(!hooks.Initialize(&error)||!runnable_executor::Initialize(hooks,env,&error)||
      !exclusive_owned_roots::Service::Bind(hooks,&service,&error))return failure(error);
  jclass system=env->FindClass("java/lang/System");
  jmethodID hash=system?env->GetStaticMethodID(system,"identityHashCode","(Ljava/lang/Object;)I"):nullptr;
  if(!hash||env->ExceptionCheck())return failure("public identity hash unavailable");
  for(int i=0;i<iterations;++i) {
    Roots roots{env,{}};
    jobject source=env->AllocObject(owner),target=env->AllocObject(owner);
    if(!source||!target||env->ExceptionCheck())return failure("owned monitor objects unavailable");
    jobject globals[]={roots.Own(source),roots.Own(target)};
    env->DeleteLocalRef(source);env->DeleteLocalRef(target);
    if(!globals[0]||!globals[1])return failure("owned monitor globals unavailable");
    jint expected=env->CallStaticIntMethod(system,hash,globals[0]);
    if(env->ExceptionCheck())return nullptr;
    if(service.With(env,globals,2,Reject,nullptr,&error)||service.With(env,globals,2,Throw,nullptr,&error))
      return failure("failed exclusive operations unexpectedly accepted");
    if(!service.With(env,globals,2,Transfer,nullptr,&error))return failure(error);
    for(jobject object:globals) {
      if(env->CallStaticIntMethod(system,hash,object)!=expected||env->ExceptionCheck())return failure("transferred identity hash disagrees with public Java");
      if(env->MonitorEnter(object)!=JNI_OK||env->MonitorExit(object)!=JNI_OK||env->ExceptionCheck())return failure("transferred monitor broke public JNI synchronization");
    }
  }
  env->DeleteLocalRef(system);return failure("");
}
#endif
