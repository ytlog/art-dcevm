#include "platform/common/jni_receiver_hook_lifetime.h"
#include "hooks/shadow_hook_engine.h"
#include "platform/arm32/readonly_metadata.h"
#include <cstdio>
#include <cinttypes>
#include <dlfcn.h>
static_assert(sizeof(void*)==4);
namespace {
using Installer=artdcevm::jni_receiver_hook_lifetime::Installer<4>;
// Pin the dispatcher image (and its ShadowHook dependency) before installing a
// process-lifetime hook. Never release these objects or the retained handle.
void LifetimeAnchor() {}
auto* Hooks(){static auto* value=new artdcevm::ShadowHookEngine;return value;}
auto* Receiver(){static auto* value=new Installer;return value;}
bool Pin(void*,std::string* error) {
  Dl_info info{};
  if(!dladdr(reinterpret_cast<void*>(LifetimeAnchor),&info) || !info.dli_fname) {
    if(error)*error="Dispatcher image unavailable";return false;
  }
  static void* retained=dlopen(info.dli_fname,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
  if(!retained){if(error)*error="Dispatcher lifetime pin refused";return false;}
  return true;
}
bool Extent(const void* pointer,size_t* output) {
  const uintptr_t address=reinterpret_cast<uintptr_t>(pointer);
  FILE* maps=std::fopen("/proc/self/maps","r");if(!maps)return false;
  char line[1024];bool found=false;
  while(std::fgets(line,sizeof(line),maps)) {
    uintptr_t start=0,end=0;char permissions[5]{};
    if(std::sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&start,&end,permissions)==3 &&
        permissions[0]=='r' && start<=address && address<end) {
      *output=end-address;found=true;break;
    }
  }
  std::fclose(maps);return found;
}
std::string Quote(const std::string& text) {
  std::string result="\"";
  for(unsigned char c:text) {
    if(c=='"' || c=='\\'){result+='\\';result+=static_cast<char>(c);}
    else if(c<32)result+='?';else result+=static_cast<char>(c);
  }
  return result+'"';
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_Arm32ReceiverProbe_nativeCalibrate(
    JNIEnv* env,jclass,jobject carrier) {
  // This ordinary JNI declaration has no FastNative/CriticalNative annotation.
  if(!env || env->ExceptionCheck())return nullptr;
  auto* hooks=Hooks();
  auto* installer=Receiver();
  size_t extent=0;std::string error;
  artdcevm::jni_receiver_calibration::Report observation{
      artdcevm::jni_receiver_calibration::Result::kUnavailable,
      artdcevm::jni_receiver_calibration::CallbackResult::kUnavailable};
  const bool passed=Extent(env,&extent) &&
      installer->InitializeFromNative(*hooks,env,carrier,extent,Pin,nullptr,&error,&observation);
  if(error.empty() && !passed)error="Current JNIEnv mapping unavailable";
  if(env->ExceptionCheck())return nullptr;
  std::string report="{\"passed\":"+std::string(passed?"true":"false")+
      ",\"hookInstalled\":"+(installer->Bound()?"true":"false")+
      ",\"receiverReady\":"+(installer->Ready()?"true":"false")+
      ",\"nativeOriginalJNIReceiverReady\":"+(passed && observation.original_callback_hits==1 && observation.matched_owner_prefixes==2?"true":"false")+
      ",\"jniPrefixReadable\":"+(passed && extent>=sizeof(artdcevm::abi::common::JniEnvironmentPrefix)?"true":"false")+
      ",\"pointerBytes\":"+std::to_string(sizeof(void*))+
      ",\"runnableScopeBytes\":"+std::to_string(sizeof(artdcevm::abi::common::RunnableJniScope))+
      ",\"jniMappedBytes\":"+std::to_string(extent)+
      ",\"originalCallbackHits\":"+std::to_string(observation.original_callback_hits)+
      ",\"matchedOwnerPrefixes\":"+std::to_string(observation.matched_owner_prefixes)+
      ",\"privateEvolutionInitialized\":false,\"publicJvmtiInitialized\":false"+
      ",\"diagnostic\":"+Quote(error)+"}";
  return env->NewStringUTF(report.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_Arm32ReceiverProbe_nativeMetadata(
 JNIEnv* env,jclass,jclass owner,jobjectArray methods,jintArray indices,jintArray flags,
 jint direct,jint virtual_count,jint class_def,jint type_index,jint class_flags,
 jobjectArray fields,jintArray field_indices,jintArray field_flags,jint static_fields,jboolean original_fields_only) {
  if(!env||env->ExceptionCheck())return nullptr;
  std::string error;size_t extent=0;
  artdcevm::arm32_readonly_metadata::Request request{};
  auto* hooks=Hooks();auto* receiver=Receiver();
  size_t method_roundtrips=0,field_roundtrips=0,consumer_methods=0,consumer_fields=0;
  jclass object_class=env->FindClass("java/lang/Object");
  jmethodID equals=object_class?env->GetMethodID(object_class,"equals","(Ljava/lang/Object;)Z"):nullptr;
  if(object_class)env->DeleteLocalRef(object_class);
  const jsize count=methods?env->GetArrayLength(methods):0;
  bool valid=equals&&owner&&count>=3&&count<=32&&indices&&flags&&
      env->GetArrayLength(indices)==count&&env->GetArrayLength(flags)==count&&
      direct>=0&&virtual_count>=0&&int64_t(direct)+virtual_count==count&&class_def>=0&&type_index>=0;
  if(valid) {
    request.owner=owner;request.extent=Extent;
    request.direct=direct;request.declared=uint32_t(int64_t(direct)+virtual_count);request.class_def=class_def;request.type_index=type_index;request.class_flags=class_flags;
    std::vector<jint> ids(count),modifiers(count);
    env->GetIntArrayRegion(indices,0,count,ids.data());env->GetIntArrayRegion(flags,0,count,modifiers.data());
    for(jsize i=0;i<count&&!env->ExceptionCheck();++i) {
      request.methods.push_back(env->GetObjectArrayElement(methods,i));
      request.indices.push_back(uint32_t(ids[i]));request.flags.push_back(uint32_t(modifiers[i]));
      jmethodID id=env->FromReflectedMethod(request.methods.back());
      jobject roundtrip=id?env->ToReflectedMethod(owner,id,(modifiers[i]&8)!=0):nullptr;
      const bool same=roundtrip&&!env->ExceptionCheck()&&env->CallBooleanMethod(roundtrip,equals,request.methods.back());
      if(roundtrip)env->DeleteLocalRef(roundtrip);
      if(!same||env->ExceptionCheck())break;
      request.method_ids.push_back(id);++method_roundtrips;
    }
    valid=!env->ExceptionCheck()&&request.methods.size()==size_t(count)&&method_roundtrips==size_t(count)&&Extent(env,&extent);
  }
  const jsize field_count=fields?env->GetArrayLength(fields):0;
  valid=valid&&fields&&field_indices&&field_flags&&field_count<=32&&static_fields>=0&&static_fields<=field_count&&
      env->GetArrayLength(field_indices)==field_count&&env->GetArrayLength(field_flags)==field_count;
  if(valid) {
    std::vector<jint> ids(field_count),modifiers(field_count);
    env->GetIntArrayRegion(field_indices,0,field_count,ids.data());env->GetIntArrayRegion(field_flags,0,field_count,modifiers.data());
    request.static_fields=static_fields;
    for(jsize i=0;i<field_count&&!env->ExceptionCheck();++i) {
      request.fields.push_back(env->GetObjectArrayElement(fields,i));request.field_indices.push_back(uint32_t(ids[i]));request.field_flags.push_back(uint32_t(modifiers[i]));
      jfieldID id=env->FromReflectedField(request.fields.back());
      jobject roundtrip=id?env->ToReflectedField(owner,id,(modifiers[i]&8)!=0):nullptr;
      const bool same=roundtrip&&!env->ExceptionCheck()&&env->CallBooleanMethod(roundtrip,equals,request.fields.back());
      if(roundtrip)env->DeleteLocalRef(roundtrip);
      if(!same||env->ExceptionCheck())break;
      request.field_ids.push_back(id);++field_roundtrips;
    }
    valid=!env->ExceptionCheck()&&request.fields.size()==size_t(field_count)&&field_roundtrips==size_t(field_count);
  }
  bool passed=false;
  if(valid) {
    // Bind the installed JNI bridge before resolving the independently named
    // object decoder. No metadata is accessed from the ordinary Native state.
    valid=receiver->InitializeFromNative(*hooks,env,request.methods[0],extent,Pin,nullptr,&error);
    request.decode=reinterpret_cast<artdcevm::arm32_readonly_metadata::Decode>(hooks->FindSymbol("libart.so",artdcevm::arm32_readonly_metadata::kDecode));
    request.id_declarations=artdcevm::canonical_jni_members::Scope::Declarations::Bind(*hooks);
    request.field_leaf=reinterpret_cast<artdcevm::arm32_readonly_metadata::FieldLeaf>(hooks->FindSymbol("libart.so",artdcevm::arm32_readonly_metadata::kFieldLeaf));
    struct Counts {size_t* methods;size_t* fields;} counts{&consumer_methods,&consumer_fields};
    request.consumer=[](const artdcevm::arm32_borrowed_metadata::Services& services,void* opaque){
      auto& observed=*static_cast<Counts*>(opaque);
      for(uint32_t i=0;i<services.methods.declared;++i){
        artdcevm::method_identity_reader::Identity identity{};
        if(!services.methods.Identity(i,&identity))return false;++*observed.methods;
      }
      for(uint32_t i=0;i<services.fields.declarations.size();++i){
        uint32_t flags=0,offset=0;if(!services.fields.Storage(i,&flags,&offset)||offset==UINT32_MAX)return false;
        ++*observed.fields;
      }
      return true;
    };
    request.consumer_data=&counts;
    if(original_fields_only)request.field_leaf=nullptr;
    if(valid)passed=receiver->InitializeFromNative(*hooks,env,request.methods[0],extent,Pin,nullptr,&error,nullptr,
        artdcevm::arm32_readonly_metadata::Observe,&request);
  }
  for(jobject method:request.methods)if(method)env->DeleteLocalRef(method);
  for(jobject field:request.fields)if(field)env->DeleteLocalRef(field);
  if(env->ExceptionCheck())return nullptr;
  if(!passed)error=request.error.empty()?(error.empty()?"Invalid complete metadata oracle":error):request.error;
  std::string report="{\"passed\":"+std::string(passed?"true":"false")+
      ",\"pointerBytes\":4,\"classProven\":"+(request.class_proven?"true":"false")+
      ",\"fieldsProven\":"+(request.fields_proven?"true":"false")+
      ",\"declaredFields\":"+std::to_string(request.fields.size())+
      ",\"staticFields\":"+std::to_string(request.static_fields)+
      ",\"decodedMethodIds\":"+std::to_string(request.decoded_methods)+
      ",\"decodedFieldIds\":"+std::to_string(request.decoded_fields)+
      ",\"methodIdRoundtrips\":"+std::to_string(method_roundtrips)+
      ",\"fieldIdRoundtrips\":"+std::to_string(field_roundtrips)+
      ",\"distinctMethodIds\":"+std::to_string(request.distinct_method_ids)+
      ",\"distinctFieldIds\":"+std::to_string(request.distinct_field_ids)+
      ",\"namedFieldCrosschecks\":"+std::to_string(request.named_field_checks)+
      ",\"originalJniFieldPath\":"+(request.fields_proven?"true":"false")+
      ",\"typedConsumerMethods\":"+std::to_string(consumer_methods)+
      ",\"typedConsumerFields\":"+std::to_string(consumer_fields)+
      ",\"methodStride\":"+std::to_string(request.proof.stride)+
      ",\"classIndexOffset\":"+std::to_string(request.class_index_offset)+
      ",\"declaredMethods\":"+std::to_string(request.proof.declared)+
      ",\"directMethods\":"+std::to_string(request.proof.direct)+
      ",\"totalMethods\":"+std::to_string(request.proof.total)+
      ",\"diagnostic\":"+Quote(error)+"}";
  return env->NewStringUTF(report.c_str());
}
