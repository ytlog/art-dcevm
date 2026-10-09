#include "platform/common/native_owned_removal.h"
#include "platform/common/native_removal_policy.h"
#include "platform/abi/arm64/android-common/class_redefinition_prefix.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android-common/jni_global_ref.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/android13/platform_string_abi.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <new>
#include <android/log.h>
namespace artdcevm::native_owned_removal {
namespace {
using NativeDefinition=abi::arm64::android_common::ClassRedefinitionPrefix;
using Check=bool(*)(void*);
using Commit=void(*)(void*,const void*);
using Structural=jvmtiError(*)(jvmtiEnv*,jint,const jvmtiClassDefinition*);
struct Item {const Definition* definition;void* validated_owner=nullptr;bool has_virtuals=false;};
struct Request {
 HookEngine* hooks;JNIEnv* env;const Provider* provider;std::vector<Item> items;
 bool committed=false;transaction_root::Holder* holder=nullptr;
 std::vector<std::pair<void*,int32_t>> remaining_owners;
 jvmtiEnv* jvmti=nullptr;JavaVM* vm=nullptr;bool graph_verified=false;bool graph_sealed=false;
};
thread_local Request* active=nullptr;
std::mutex installation;
void* fields_original=nullptr;void* methods_original=nullptr;void* commit_original=nullptr;
void (*record_fields)(void*)=nullptr;void (*record_methods)(void*)=nullptr;

bool installed=false;
bool window_ready=false;
using Verified=bool(*)(void*,transaction_root::Holder*);
using Failure=void(*)(void*,jvmtiError,const void*,const void*);
Verified verified_original=nullptr,collect_original=nullptr;Failure failure=nullptr;
abi::arm64::android_common::AddGlobalRef add_global=nullptr;
abi::arm64::android11::CurrentThread current_thread=nullptr;
void* (*root_barrier)(void*)=nullptr;
bool Mapped(const void* p,size_t need){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=need;}
template<class T>T Read(const void* p,size_t at){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
Item* Matches(void* owner){
  if(!active||!Mapped(owner,sizeof(NativeDefinition)))return nullptr;
  const auto definition=Read<NativeDefinition>(owner,0);
  if(!definition.klass||!definition.dex_file||!Mapped(definition.dex_file,16))return nullptr;
  Item* matched=nullptr;
  for(auto& item:active->items){
    if(active->env->IsSameObject(definition.klass,item.definition->klass)){
      if(matched||active->env->ExceptionCheck())return nullptr;matched=&item;
    }
  }
  if(!matched||active->env->ExceptionCheck()||
     (matched->validated_owner&&matched->validated_owner!=owner))return nullptr;
  const auto begin=Read<const unsigned char*>(definition.dex_file,8);
  if(!Mapped(begin,112)||std::memcmp(begin,"dex\n",4)!=0)return nullptr;
  const auto size=Read<uint32_t>(begin,32);const auto& bytes=matched->definition->dex;
  if(size!=bytes.size()||!Mapped(begin,size)||std::memcmp(begin,bytes.data(),size)!=0)return nullptr;
  for(const auto& item:active->items)if(&item!=matched&&item.validated_owner==owner)return nullptr;
  matched->validated_owner=owner;
  if(matched->has_virtuals){const bool value=true;std::memcpy(static_cast<unsigned char*>(owner)+offsetof(NativeDefinition,has_virtuals),&value,sizeof(value));}
  return matched;
}
bool Fields(void* owner){if(!active||!active->provider->allow_field_removal||!Matches(owner))return reinterpret_cast<Check>(fields_original)(owner);record_fields(owner);return true;}
bool Methods(void* owner){if(!Matches(owner))return reinterpret_cast<Check>(methods_original)(owner);record_methods(owner);return true;}
bool CaptureGraph(void* driver,transaction_root::Holder* raw){
 if(!active||!active->provider->closed_graph)return true;
 auto& request=*active;std::string error;
 const auto reject=[&](const char* reason){
  android13::BorrowedPlatformString signature("OWNED BATCH"),message(reason);
  failure(driver,JVMTI_ERROR_NOT_AVAILABLE,&signature,&message);return false;
 };
 if(!Mapped(raw,sizeof(*raw)))return reject("Repeated or unmapped precommit Holder");
 transaction_root::Holder holder{};size_t extent=0;method_identity::ReadableMappingExtent(raw,&extent);
 if(!transaction_root::ReadHolderHeader(raw,extent,&holder)||!Mapped(holder.array_handle,4)||
    !Mapped(holder.redefinitions,sizeof(transaction_root::Owners)))return reject("Invalid precommit Holder native header");
 for(const auto& item:request.items){
  if(!item.validated_owner||Read<NativeDefinition>(item.validated_owner,0).driver!=driver)return reject("Precommit driver has unvalidated native owner");
 }
 // This hook inherits RedefineClassesDirect's actual shared-mutator Runnable
 // scope. Root the handle immediately, before any JNI state transition. All
 // subsequent managed accesses use public JNI rooted arrays, never raw roots.
 void* self=current_thread();void* array=root_barrier(const_cast<uint32_t*>(holder.array_handle));
 if(!self||!Mapped(array,12))return reject("Precommit root barrier unavailable");
 jobject global=add_global(request.vm,self,{uintptr_t(array)});
 if(!global)return reject("Precommit array could not be rooted");
 struct Global {JNIEnv* env;jobject object;~Global(){env->DeleteGlobalRef(object);}} global_scope{request.env,global};
 if(request.env->ExceptionCheck())return reject("Precommit rooting left a pending exception");
 const jsize slots=request.env->GetArrayLength(static_cast<jarray>(global));
 transaction_root::Metadata metadata{};
 if(request.env->ExceptionCheck()||slots<=0||slots%abi::arm64::android11::kRootSlots||
    size_t(slots)/abi::arm64::android11::kRootSlots!=request.items.size()||
    !transaction_root::ReadMetadata(holder,request.items.size(),transaction_root::OwnerFamily::kAndroid15,&metadata)||
    !Mapped(metadata.initial_structural.begin,8*((metadata.count+63)/64)))return reject("Precommit root partition disagrees with whole batch");
 std::vector<jclass> classes;
 struct Locals {JNIEnv* env;std::vector<jclass>& values;~Locals(){for(auto value:values)env->DeleteLocalRef(value);}} locals{request.env,classes};
 for(size_t index=0;index<metadata.count;++index){
  if(!transaction_root::Bit(metadata.initial_structural,index))continue;
  jobject objects=request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kOldClassesSlot);
  if(request.env->ExceptionCheck()||!objects)return reject("Initial island is missing actual oldClasses roots");
  struct Local {JNIEnv* env;jobject value;~Local(){env->DeleteLocalRef(value);}} local{request.env,objects};
  const jsize count=request.env->GetArrayLength(static_cast<jarray>(objects));
  if(request.env->ExceptionCheck()||count<=0)return reject("Empty affected Class island");
  for(jsize n=0;n<count;++n){
   auto klass=static_cast<jclass>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(objects),n));
   if(request.env->ExceptionCheck()||!klass)return reject("Null affected Class root");
   bool duplicate=false;for(auto previous:classes)if(request.env->IsSameObject(previous,klass))duplicate=true;
   if(duplicate){request.env->DeleteLocalRef(klass);return reject("Duplicate affected Class root");}
   classes.push_back(klass);
  }
 }
 if(classes.empty()||!request.provider->closed_graph(request.env,request.jvmti,classes,request.provider->state,&error))
  return reject(error.empty()?"Affected Class graph was not independently staged":error.c_str());
 request.graph_verified=true;return true;
}
bool GuardGraph(void* driver,transaction_root::Holder* raw){
 try{return CaptureGraph(driver,raw);}
 catch(const std::bad_alloc&){
  android13::BorrowedPlatformString signature("OWNED BATCH"),message("Precommit graph staging exhausted native memory");
  failure(driver,JVMTI_ERROR_OUT_OF_MEMORY,&signature,&message);return false;
 }
}
bool VerifiedGraph(void* driver,transaction_root::Holder* raw){
 if(!verified_original(driver,raw))return false;
 if(active&&active->provider->closed_graph&&active->graph_verified){
  android13::BorrowedPlatformString signature("OWNED BATCH"),message("Repeated verification boundary");failure(driver,JVMTI_ERROR_NOT_AVAILABLE,&signature,&message);return false;}
 return GuardGraph(driver,raw);
}
bool CollectGraph(void* driver,transaction_root::Holder* raw){
 if(!collect_original(driver,raw))return false;
 if(!active||!active->provider->closed_graph)return true;
 if(!active->graph_verified||active->graph_sealed){android13::BorrowedPlatformString signature("OWNED BATCH"),message("Invalid final rejectable boundary");failure(driver,JVMTI_ERROR_NOT_AVAILABLE,&signature,&message);return false;}
 if(!GuardGraph(driver,raw))return false;
 active->graph_sealed=true;return true;
}
void Update(void* owner,const void* iterator){
  if(!active||std::none_of(active->items.begin(),active->items.end(),[&](const auto& item){return item.validated_owner==owner;})){
    reinterpret_cast<Commit>(commit_original)(owner,iterator);return;
  }
  transaction_root::Iterator current{};
  if(active->provider->closed_graph&&!active->graph_sealed)std::abort();
  size_t extent=0;
  if(!method_identity::ReadableMappingExtent(iterator,&extent)||!transaction_root::ReadIterator(iterator,extent,&current))std::abort();
  if(active->committed){
    // JNI and mutable managed roots are deliberately not revisited after heap
    // replacement. Only the fixed Holder/native owner previously proven remains.
    auto found=std::find(active->remaining_owners.begin(),active->remaining_owners.end(),std::make_pair(owner,current.index));
    if(current.holder!=active->holder||found==active->remaining_owners.end())std::abort();
    active->remaining_owners.erase(found);return;
  }
  std::string error;owned_structural_transaction::StructuralTransaction first;
  if(!owned_structural_transaction::ReadStructuralTransaction(*active->hooks,*active->provider->metadata,iterator,&first,&error)||first.roots.owner!=owner||first.metadata.count!=active->items.size()){
    __android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned batch structural roots: %s",error.c_str());std::abort();
  }
  for(const auto& item:active->items)if(!item.validated_owner)std::abort();
  source_commit::Plan combined;combined.mirror=first.plan.mirror;combined.new_classes_root=first.plan.new_classes_root;
  active->holder=current.holder;
  for(size_t index=0;index<first.metadata.count;++index){
    void* native_owner=const_cast<unsigned char*>(first.metadata.owners.begin)+index*first.metadata.stride;
    if(std::count_if(active->items.begin(),active->items.end(),[&](const auto& item){return item.validated_owner==native_owner;})!=1)std::abort();
    if(!transaction_root::Bit(first.metadata.initial_structural,index))continue;
    transaction_root::Iterator cursor{static_cast<int32_t>(index),current.holder};
    owned_structural_transaction::StructuralTransaction part;
    if(!owned_structural_transaction::ReadStructuralTransaction(*active->hooks,*active->provider->metadata,&cursor,&part,&error)||part.roots.owner!=native_owner)std::abort();
    const auto& plan=part.plan;
    for(size_t n=0;n<plan.old_classes.size();++n){
      if(std::find(combined.old_classes.begin(),combined.old_classes.end(),plan.old_classes[n])!=combined.old_classes.end()||
         std::find(combined.new_classes.begin(),combined.new_classes.end(),plan.new_classes[n])!=combined.new_classes.end())std::abort();
      combined.old_classes.push_back(plan.old_classes[n]);combined.new_classes.push_back(plan.new_classes[n]);
      combined.class_root_origins.push_back({plan.new_classes_root,n});
    }
    // Each actual partition independently proved instance Class correspondence.
    // Reject overlapping islands; no unrooted synthetic managed array is made.
    for(size_t n=0;n<plan.old_instances.size();++n){
      if(std::find(combined.old_instances.begin(),combined.old_instances.end(),plan.old_instances[n])!=combined.old_instances.end()||
         std::find(combined.new_instances.begin(),combined.new_instances.end(),plan.new_instances[n])!=combined.new_instances.end())std::abort();
      combined.old_instances.push_back(plan.old_instances[n]);combined.new_instances.push_back(plan.new_instances[n]);
    }
    combined.abstract_instances|=plan.abstract_instances;
    if(native_owner!=owner)active->remaining_owners.emplace_back(native_owner,static_cast<int32_t>(index));
  }
  auto services=active->provider->prepare(combined,active->provider->state,&error);
  if(!services||!source_commit::Run(combined,*services)){
    __android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned batch preparation/commit invariant: %s",error.c_str());std::abort();
  }
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
    {"_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE",reinterpret_cast<void*>(Update),&commit_original},
    {"_ZN12openjdkjvmti9Redefiner26CheckAllClassesAreVerifiedERNS_22RedefinitionDataHolderE",reinterpret_cast<void*>(VerifiedGraph),reinterpret_cast<void**>(&verified_original)},
    {"_ZN12openjdkjvmti9Redefiner28CollectAndCreateNewInstancesERNS_22RedefinitionDataHolderE",reinterpret_cast<void*>(CollectGraph),reinterpret_cast<void**>(&collect_original)}};
  for(auto& entry:entries)entry.target=hooks.FindSymbol("libopenjdkjvmti.so",entry.symbol);
  record_fields=reinterpret_cast<decltype(record_fields)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition19RecordNewFieldAddedEv"));
  record_methods=reinterpret_cast<decltype(record_methods)>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"));
  failure=reinterpret_cast<Failure>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner13RecordFailureE10jvmtiErrorRKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEESA_"));
  add_global=reinterpret_cast<abi::arm64::android_common::AddGlobalRef>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kAddGlobalRef));
  current_thread=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
  root_barrier=reinterpret_cast<decltype(root_barrier)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!record_fields||!record_methods||!failure||!add_global||!current_thread||!root_barrier||!construct||!destroy||
     std::any_of(std::begin(entries),std::end(entries),[](const auto& entry){return !entry.target;})){
    if(error)*error="Missing named owned structural entry";return false;
  }
  SuspendAll stop{};construct(&stop,"artdcevm native source owner",false);
  std::vector<void*> tokens;bool okay=true;
  for(auto& entry:entries){void* token=hooks.Install(entry.target,entry.proxy,entry.original,error);
    if(!token){okay=false;break;}tokens.push_back(token);if(!*entry.original)std::abort();}
  if(!okay)for(auto at=tokens.rbegin();at!=tokens.rend();++at){std::string rollback;if(!hooks.Remove(*at,&rollback))std::abort();}
  destroy(&stop);installed=okay;window_ready=okay;return okay;
}
bool GraphWindowReady(){return window_ready;}
jvmtiError RunBatch(HookEngine& hooks,JNIEnv* env,jvmtiEnv* jvmti,
    const std::vector<Definition>& definitions,const Provider& provider,std::string* error){
  if(!env||!jvmti||definitions.empty()||definitions.size()>INT32_MAX||active||!installed||!provider.ready||!provider.prepare||
     !provider.metadata||!provider.metadata->Calibrated()||!provider.ready(provider.state)||
     (definitions.size()>1&&!provider.preflight)){
    if(error)*error="Owned batch provider is not fully prepared";return JVMTI_ERROR_NOT_AVAILABLE;
  }
  Request request{&hooks,env,&provider,{},false,nullptr,{}};bool removed=false;
  request.jvmti=jvmti;
  if(provider.closed_graph&&(!window_ready||env->GetJavaVM(&request.vm)!=JNI_OK||!request.vm)){
    if(error)*error="Named rejectable graph window is not bound";return JVMTI_ERROR_NOT_AVAILABLE;
  }
  struct Namespace {std::string descriptor;jobject loader;};std::vector<Namespace> names;
  struct Loaders {JNIEnv* env;std::vector<Namespace>& names;~Loaders(){for(const auto& entry:names)if(entry.loader)env->DeleteLocalRef(entry.loader);}} release{env,names};
  for(const auto& definition:definitions){
    if(!definition.klass)return JVMTI_ERROR_INVALID_CLASS;
    for(const auto& item:request.items)if(env->IsSameObject(item.definition->klass,definition.klass)){
      if(error)*error="Same Class appears twice in the owned batch";return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    }
    if(env->ExceptionCheck())return JVMTI_ERROR_INTERNAL;
    evolution::ClassSchema next,old;
    if(definition.dex.empty()||definition.dex.size()>INT32_MAX||!evolution::ReadDexDeclarations(definition.dex,&next,error))return JVMTI_ERROR_INVALID_CLASS_FORMAT;
    const int read=android13::ReadLiveSchema(jvmti,env,definition.klass,&old,error);
    if(read!=JVMTI_ERROR_NONE)return static_cast<jvmtiError>(read);
    if(!ValidateRemoval(old,next,provider.allow_field_removal,error,false))return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
    jobject loader=nullptr;const auto loader_error=jvmti->GetClassLoader(definition.klass,&loader);
    if(loader_error!=JVMTI_ERROR_NONE)return loader_error;
    bool duplicate=false;for(const auto& name:names)if(name.descriptor==old.descriptor&&env->IsSameObject(name.loader,loader))duplicate=true;
    if(duplicate||env->ExceptionCheck()){if(loader)env->DeleteLocalRef(loader);if(error)*error="Duplicate class namespace in owned batch";return JVMTI_ERROR_ILLEGAL_ARGUMENT;}
    names.push_back({old.descriptor,loader});
    removed|=old.methods.size()!=next.methods.size()||old.fields.size()!=next.fields.size();
    Item item{&definition};item.has_virtuals=std::any_of(next.fields.begin(),next.fields.end(),[](const auto& member){return !member.IsStatic();})||
      std::any_of(next.methods.begin(),next.methods.end(),[](const auto& member){return !(member.access&(0x8u|0x2u))&&member.name!="<init>"&&member.name!="<clinit>";});
    request.items.push_back(item);
  }
  if(!removed){if(error)*error="Owned batch requires at least one actual member removal";return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;}
  if(provider.preflight&&!provider.preflight(env,jvmti,definitions,provider.state,error))return JVMTI_ERROR_NOT_AVAILABLE;
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
  struct Window {explicit Window(Request& r){active=&r;}~Window(){active=nullptr;}} window(request);
  std::vector<jvmtiClassDefinition> incoming;incoming.reserve(definitions.size());
  for(const auto& definition:definitions)incoming.push_back({definition.klass,static_cast<jint>(definition.dex.size()),definition.dex.data()});
  const auto result=structural(jvmti,static_cast<jint>(incoming.size()),incoming.data());
  if(request.committed&&(result!=JVMTI_ERROR_NONE||!request.remaining_owners.empty()))std::abort();
  if(result==JVMTI_ERROR_NONE&&!request.committed){
    if(error)*error="Source entry did not consume every owned batch structural owner";return JVMTI_ERROR_INTERNAL;
  }
  if(result==JVMTI_ERROR_NONE&&error)error->clear();return result;
}
jvmtiError Run(HookEngine& hooks,JNIEnv* env,jvmtiEnv* jvmti,jclass target,
    const std::vector<unsigned char>& incoming,const Provider& provider,std::string* error){
  return RunBatch(hooks,env,jvmti,{{target,incoming}},provider,error);
}
}
