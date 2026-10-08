// Debug-only public-reflection oracle for the production metadata service.
#ifdef ARTDCEVM_TESTING
#include "platform/common/modern_field_metadata.h"
#include "platform/common/runnable_executor.h"
#include "hooks/shadow_hook_engine.h"
#include <vector>
namespace {
using namespace artdcevm;
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
};
bool Observe(const runnable_jni::Context& scope,void* data) {
  auto& request=*static_cast<Request*>(data);
  return request.metadata.Calibrate(scope,request.owner,request.descriptor.c_str(),
      request.fields->data(),request.fields->size(),&request.error);
}
std::string String(JNIEnv* env,jstring value) {
  if(!value)return {};
  const char* bytes=env->GetStringUTFChars(value,nullptr);
  if(!bytes)return {};
  std::string text(bytes);env->ReleaseStringUTFChars(value,bytes);return text;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeFields(
    JNIEnv* env,jclass,jclass owner,jstring descriptor,jobjectArray fields,
    jobjectArray names,jobjectArray types,jintArray modifiers) {
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
  Request request{{},global_owner,&oracles,String(env,descriptor),{}};
  ShadowHookEngine hooks;
  if(!hooks.Initialize(&request.error)||!runnable_executor::Initialize(hooks,env,&request.error)||
      !request.metadata.Bind(hooks,&request.error))return failure(request.error);
  auto result=runnable_executor::With(env,global_owner,Observe,&request);
  if(result!=runnable_jni::Result::kSuccess)return failure("field metadata: "+request.error);
  return failure(request.metadata.StorageModel()==modern_field_metadata::Model::kMerged?"merged":"split");
}
#endif
