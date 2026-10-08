#include "platform/common/native_owned_removal.h"
#include "platform/common/native_removal_policy.h"
#include "platform/abi/arm64/android11/class_redefinition.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/common/method_identity_calibration.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <android/log.h>
namespace artdcevm::native_owned_removal {
namespace {
using Definition=abi::arm64::android11::ClassRedefinition;
using Check=bool(*)(void*);
using Commit=void(*)(void*,const void*);
using Structural=jvmtiError(*)(jvmtiEnv*,jint,const jvmtiClassDefinition*);
struct Request {HookEngine* hooks;JNIEnv* env;jclass target;const std::vector<unsigned char>* dex;const Provider* provider;void* validated_owner=nullptr;bool has_virtuals=false;bool committed=false;};
thread_local Request* active=nullptr;
std::mutex installation;
void* fields_original=nullptr;void* methods_original=nullptr;void* commit_original=nullptr;
void (*record_fields)(void*)=nullptr;void (*record_methods)(void*)=nullptr;

bool installed=false;
bool Mapped(const void* p,size_t need){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=need;}
template<class T>T Read(const void* p,size_t at){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
bool Matches(void* owner){
  if(!active||!Mapped(owner,sizeof(Definition)))return false;
  const auto definition=Read<Definition>(owner,0);
  if(!definition.klass||!definition.dex_file||!Mapped(definition.dex_file,16)||
     !active->env->IsSameObject(definition.klass,active->target)||active->env->ExceptionCheck())return false;
  // Fixed DexFile common header (vptr, begin, size) is authorized only by the
  // independently supplied incoming bytes: all bytes and size must agree.
  const auto begin=Read<const unsigned char*>(definition.dex_file,8);
  // Android 17 DexFile::unused_size_ is not a live size. Authorize the
  // shared begin pointer with the independent complete standard DEX input.
  if(!Mapped(begin,112)||std::memcmp(begin,"dex\n",4)!=0)return false;
  const auto size=Read<uint32_t>(begin,32);
  if(size!=active->dex->size()||!Mapped(begin,size)||
      std::memcmp(begin,active->dex->data(),size)!=0||
      (active->validated_owner&&active->validated_owner!=owner))return false;
  active->validated_owner=owner;
  if(active->has_virtuals){const bool value=true;std::memcpy(static_cast<unsigned char*>(owner)+offsetof(Definition,has_virtuals),&value,sizeof(value));}
  return true;
}
bool Fields(void* owner){if(!active||!active->provider->allow_field_removal||!Matches(owner))return reinterpret_cast<Check>(fields_original)(owner);record_fields(owner);return true;}
bool Methods(void* owner){if(!Matches(owner))return reinterpret_cast<Check>(methods_original)(owner);record_methods(owner);return true;}
void Update(void* owner,const void* iterator){
  if(!active||active->validated_owner!=owner){reinterpret_cast<Commit>(commit_original)(owner,iterator);return;}
  // This is the original final ART exclusive mutator/disabled-GC boundary.
  // Public JNI is forbidden here; verify target through the Holder + named
  // native mirror oracle, rather than calling Matches/IsSameObject again.
  owned_structural_transaction::StructuralTransaction transaction;std::string error;
  if(!owned_structural_transaction::ReadStructuralTransaction(*active->hooks,
      *active->provider->metadata,iterator,&transaction,&error)||transaction.roots.owner!=owner)
    {__android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned structural root precondition: %s",error.c_str());std::abort();}
  auto services=active->provider->prepare(transaction.plan,active->provider->state,&error);
  if(!services||!source_commit::Run(transaction.plan,*services)){__android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned structural preparation/commit invariant: %s",error.c_str());std::abort();}
  active->committed=true;
}

}
bool Install(HookEngine& hooks,std::string* error){
  std::lock_guard<std::mutex> guard(installation);
  if(installed)return true;
  if(!hooks.Initialize(error))return false;
  struct Entry {const char* symbol;void* proxy;void** original;void* target=nullptr;};
  Entry entries[]{
    {"_ZN12openjdkjvmti9Redefiner17ClassRedefinition11CheckFieldsEv",reinterpret_cast<void*>(Fields),&fields_original},
    {"_ZN12openjdkjvmti9Redefiner17ClassRedefinition12CheckMethodsEv",reinterpret_cast<void*>(Methods),&methods_original},
    {"_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE",reinterpret_cast<void*>(Update),&commit_original}};
  for(auto& entry:entries)entry.target=hooks.FindSymbol("libopenjdkjvmti.so",entry.symbol);
  record_fields=reinterpret_cast<decltype(record_fields)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition19RecordNewFieldAddedEv"));
  record_methods=reinterpret_cast<decltype(record_methods)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"));
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!record_fields||!record_methods||!construct||!destroy||
     std::any_of(std::begin(entries),std::end(entries),[](const auto& entry){return !entry.target;})){
    if(error)*error="Missing named owned structural entry";return false;
  }
  SuspendAll stop{};construct(&stop,"artdcevm native source owner",false);
  std::vector<void*> tokens;bool okay=true;
  for(auto& entry:entries){void* token=hooks.Install(entry.target,entry.proxy,entry.original,error);
    if(!token){okay=false;break;}tokens.push_back(token);if(!*entry.original)std::abort();}
  if(!okay)for(auto at=tokens.rbegin();at!=tokens.rend();++at){std::string rollback;if(!hooks.Remove(*at,&rollback))std::abort();}
  destroy(&stop);installed=okay;return okay;
}
jvmtiError Run(HookEngine& hooks,JNIEnv* env,jvmtiEnv* jvmti,jclass target,
    const std::vector<unsigned char>& incoming,const Provider& provider,std::string* error){
  if(!env||!jvmti||!target||active||!installed||!provider.ready||!provider.prepare||
     !provider.metadata||!provider.metadata->Calibrated()||
     !provider.ready(provider.state)){
    if(error)*error="Owned removal provider is not fully prepared";return JVMTI_ERROR_NOT_AVAILABLE;
  }
  evolution::ClassSchema next,old;
  if(incoming.empty()||incoming.size()>INT32_MAX||!evolution::ReadDexDeclarations(incoming,&next,error))return JVMTI_ERROR_INVALID_CLASS_FORMAT;
  const int read=android13::ReadLiveSchema(jvmti,env,target,&old,error);
  if(read!=JVMTI_ERROR_NONE)return static_cast<jvmtiError>(read);
  if(!ValidateRemoval(old,next,provider.allow_field_removal,error))return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
  jint count=0;jvmtiExtensionFunctionInfo* functions=nullptr;Structural structural=nullptr;
  const auto extension_error=jvmti->GetExtensionFunctions(&count,&functions);
  if(extension_error!=JVMTI_ERROR_NONE)return extension_error;
  for(jint n=0;n<count;++n)if(functions[n].id&&!std::strcmp(functions[n].id,"com.android.art.class.structurally_redefine_classes"))
    structural=reinterpret_cast<Structural>(functions[n].func);
  if(functions){for(jint n=0;n<count;++n){auto& f=functions[n];
    if(f.id)jvmti->Deallocate(reinterpret_cast<unsigned char*>(f.id));
    if(f.short_description)jvmti->Deallocate(reinterpret_cast<unsigned char*>(f.short_description));
    for(jint j=0;j<f.param_count;++j)if(f.params[j].name)jvmti->Deallocate(reinterpret_cast<unsigned char*>(f.params[j].name));
    if(f.params)jvmti->Deallocate(reinterpret_cast<unsigned char*>(f.params));
    if(f.errors)jvmti->Deallocate(reinterpret_cast<unsigned char*>(f.errors));
  }jvmti->Deallocate(reinterpret_cast<unsigned char*>(functions));}
  if(!structural){if(error)*error="Missing public owned structural extension";return JVMTI_ERROR_NOT_AVAILABLE;}
  Request request{&hooks,env,target,&incoming,&provider};
  request.has_virtuals=std::any_of(next.fields.begin(),next.fields.end(),[](const auto& member){return !member.IsStatic();})||
      std::any_of(next.methods.begin(),next.methods.end(),[](const auto& member){return !(member.access&(0x8u|0x2u))&&member.name!="<init>"&&member.name!="<clinit>";});
  struct Window {explicit Window(Request& r){active=&r;}~Window(){active=nullptr;}} window(request);
  const jvmtiClassDefinition definition{target,static_cast<jint>(incoming.size()),incoming.data()};
  const auto result=structural(jvmti,1,&definition);
  if(result==JVMTI_ERROR_NONE&&!request.committed){if(error)*error="Source entry did not reach owned final structural commit";return JVMTI_ERROR_INTERNAL;}
  if(result==JVMTI_ERROR_NONE&&error)error->clear();return result;
}
}
