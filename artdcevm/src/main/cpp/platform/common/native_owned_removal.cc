#include "platform/common/native_owned_removal.h"
#include "platform/common/native_removal_policy.h"
#include "platform/common/owned_failure_diagnostic.h"
#include "platform/common/native_schema_policy.h"
#include "platform/abi/arm64/android-common/class_redefinition_prefix.h"
#include "platform/abi/arm64/android-common/redefinition_boundaries.h"
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
#include <cstdio>
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
 transaction_root::Holder* boundary_holder=nullptr;size_t verified_count=0,collected_count=0;
 const char* phase="before original source checks";char rejection[768]{};
 void* source_driver=nullptr;bool source_run_active=false,refunded=false,publication_started=false;
 size_t completed_validated=0;
 void* verification_owner=nullptr;const transaction_root::Iterator* verification_iterator=nullptr;
};
thread_local Request* active=nullptr;
void Phase(const char* text) noexcept {if(active)active->phase=text;}
void RememberFailure(const char* text) noexcept {
 if(!active||!text||active->rejection[0])return;
 size_t length=0;while(length+1<sizeof(active->rejection)&&text[length])++length;
 // Do not split a UTF-8/modified-UTF-8 code unit before the caller JNI string.
 while(length&&text[length]&&(static_cast<unsigned char>(text[length])&0xc0u)==0x80u)--length;
 std::memcpy(active->rejection,text,length);active->rejection[length]=0;
}
void CopyFailure(const Request& request,jvmtiError code,std::string* error) noexcept {
 if(!error)return;
 char text[1024];const size_t validated=request.completed_validated+std::count_if(request.items.begin(),request.items.end(),[](const auto& item){return item.validated_owner!=nullptr;});
 std::snprintf(text,sizeof(text),"Owned source error %d; last observed boundary=%s: %s [validated=%zu/%zu verifiedLeaves=%zu collectedLeaves=%zu graphVerified=%d graphSealed=%d]",int(code),request.phase,
   request.rejection[0]?request.rejection:"original source returned failure without an observed owned refusal",validated,request.items.size(),request.verified_count,request.collected_count,int(request.graph_verified),int(request.graph_sealed));
 // The original source already returned. Diagnostics never replace its code,
 // and even a caller-string allocation failure must not escape that boundary.
 owned_failure_diagnostic::Publish(text,error);
}
bool Lifecycle(const Provider& p) noexcept {return p.begin_run||p.end_run||p.before_verification||p.after_verification||p.refund||p.commit;}
bool CompleteLifecycle(const Provider& p) noexcept {return p.begin_run&&p.end_run&&p.before_verification&&p.after_verification&&p.refund&&p.commit;}
void CloseVerification(bool result) noexcept {
 if(!active||!active->verification_owner)return;
 auto& r=*active;void* owner=r.verification_owner;auto* iter=r.verification_iterator;
 r.verification_owner=nullptr;r.verification_iterator=nullptr;
 r.provider->after_verification(owner,iter,result,r.provider->state);
}
void Refund() noexcept {
 if(!active||!Lifecycle(*active->provider)||!active->source_run_active||active->refunded||active->publication_started)return;
 CloseVerification(false);active->refunded=true;active->provider->refund(active->provider->state);
}
std::mutex installation;
void* fields_original=nullptr;void* methods_original=nullptr;void* commit_original=nullptr;
void (*record_fields)(void*)=nullptr;void (*record_methods)(void*)=nullptr;

bool installed=false;
bool window_ready=false;
namespace boundaries=abi::arm64::android_common::redefinition_boundaries;
using Verified=boundaries::Whole;
using Failure=void(*)(void*,jvmtiError,const void*,const void*);
Verified verified_original=nullptr,collect_original=nullptr;Failure failure=nullptr;
boundaries::Run run_original=nullptr;bool run_ready=false;
using VerificationLeaf=boundaries::VerifyClass;
using CollectionLeaf=boundaries::CollectClass;
VerificationLeaf verification_leaf_original=nullptr;CollectionLeaf collection_leaf_original=nullptr;
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
bool Fields(void* owner){Phase("Class CheckFields entry");if(!active||!active->provider->allow_field_removal||!Matches(owner)){
 const bool result=reinterpret_cast<Check>(fields_original)(owner);if(!result){RememberFailure("Original CheckFields rejected the source definition");Refund();}return result;}record_fields(owner);return true;}
bool Methods(void* owner){Phase("Class CheckMethods entry");if(!Matches(owner)){
 const bool result=reinterpret_cast<Check>(methods_original)(owner);if(!result){RememberFailure("Original CheckMethods rejected the source definition");Refund();}return result;}record_methods(owner);return true;}
bool CaptureGraph(void* driver,transaction_root::Holder* raw,GraphWindow window){
 if(!active||!active->provider->closed_graph)return true;
 auto& request=*active;Phase(window==GraphWindow::Verified?"Verified graph capture":"Collected graph preparation");std::string error;
 const auto reject=[&](const char* reason){
  RememberFailure(reason);
  android13::BorrowedPlatformString signature("OWNED BATCH"),message(reason);
  failure(driver,JVMTI_ERROR_NOT_AVAILABLE,&signature,&message);Refund();return false;
 };
 if(!Mapped(raw,sizeof(*raw))||(request.boundary_holder&&request.boundary_holder!=raw))return reject("Repeated, changed or unmapped precommit Holder");
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
 RootedGraph graph;graph.holder=raw;std::vector<jobject> arrays;
 struct ArrayLocals {JNIEnv* env;std::vector<jobject>& values;~ArrayLocals(){for(auto value:values)env->DeleteLocalRef(value);}} array_locals{request.env,arrays};
 std::vector<jclass> classes;
 struct Locals {JNIEnv* env;std::vector<jclass>& values;~Locals(){for(auto value:values)env->DeleteLocalRef(value);}} locals{request.env,classes};
 for(size_t index=0;index<metadata.count;++index){
  if(!transaction_root::Bit(metadata.initial_structural,index))continue;
  jobject objects=request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kOldClassesSlot);
  if(request.env->ExceptionCheck()||!objects)return reject("Initial island is missing actual oldClasses roots");
  arrays.push_back(objects);
  const jsize count=request.env->GetArrayLength(static_cast<jarray>(objects));
  if(request.provider->schema_evolution){
  auto replacements=static_cast<jobjectArray>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kNewClassesSlot));
  if(request.env->ExceptionCheck()||!replacements)return reject("Missing replacement Class Holder partition");arrays.push_back(replacements);
  if(request.env->GetArrayLength(replacements)!=count)return reject("Mismatched original/replacement Class partition");
  RootedPartition partition{static_cast<jobjectArray>(objects),replacements,nullptr,nullptr,index,size_t(count),0};
  if(!graph.mirror){graph.mirror=static_cast<jclass>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kMirrorSlot));if(!graph.mirror||request.env->ExceptionCheck())return reject("Missing source mirror Class");arrays.push_back(graph.mirror);}
  if(window==GraphWindow::Collected){
    partition.old_instances=static_cast<jobjectArray>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kOldInstancesSlot));
    partition.new_instances=static_cast<jobjectArray>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(global),index*abi::arm64::android11::kRootSlots+abi::arm64::android11::kNewInstancesSlot));
    if(partition.old_instances)arrays.push_back(partition.old_instances);if(partition.new_instances)arrays.push_back(partition.new_instances);
    if(request.env->ExceptionCheck()||bool(partition.old_instances)!=bool(partition.new_instances))return reject("Mismatched instance Holder partitions");
    if(partition.old_instances){const jsize instances=request.env->GetArrayLength(partition.old_instances);if(instances<0||request.env->GetArrayLength(partition.new_instances)!=instances||request.env->ExceptionCheck())return reject("Mismatched instance Holder lengths");partition.instance_count=size_t(instances);}
  }
  graph.partitions.push_back(partition);
  }
  if(request.env->ExceptionCheck()||count<=0)return reject("Empty affected Class island");
  for(jsize n=0;n<count;++n){
   auto klass=static_cast<jclass>(request.env->GetObjectArrayElement(static_cast<jobjectArray>(objects),n));
   if(request.env->ExceptionCheck()||!klass)return reject("Null affected Class root");
   bool duplicate=false;for(auto previous:classes)if(request.env->IsSameObject(previous,klass))duplicate=true;
   if(duplicate){request.env->DeleteLocalRef(klass);return reject("Duplicate affected Class root");}
   classes.push_back(klass);
  }
 }
 if(classes.empty()||!request.provider->closed_graph(request.env,request.jvmti,classes,graph,window,request.provider->state,&error))
  return reject(error.empty()?"Affected Class graph was not independently staged":error.c_str());
 if(request.provider->schema_evolution&&window==GraphWindow::Collected){
  request.holder=raw;request.remaining_owners.clear();
  for(const auto& partition:graph.partitions){
   void* owner=const_cast<unsigned char*>(metadata.owners.begin)+partition.holder_index*metadata.stride;
   if(std::count_if(request.items.begin(),request.items.end(),[&](const auto& item){return item.validated_owner==owner;})!=1)return reject("Collected Holder has an unowned structural partition");
   if(request.remaining_owners.size()==request.remaining_owners.capacity())return reject("Collected owner storage was not reserved");
   request.remaining_owners.emplace_back(owner,static_cast<int32_t>(partition.holder_index));
  }
 }
 request.boundary_holder=raw;request.graph_verified=true;
 __android_log_print(ANDROID_LOG_INFO,"artdcevm","Owned graph window %s: verifiedLeaves=%zu collectedLeaves=%zu owners=%zu",window==GraphWindow::Verified?"verified":"collected",request.verified_count,request.collected_count,request.items.size());return true;
}
bool GuardGraph(void* driver,transaction_root::Holder* raw,GraphWindow window){
 try{return CaptureGraph(driver,raw,window);}
 catch(const std::bad_alloc&){
  RememberFailure("Precommit graph staging exhausted native memory");
  android13::BorrowedPlatformString signature("OWNED BATCH"),message("Precommit graph staging exhausted native memory");
  failure(driver,JVMTI_ERROR_OUT_OF_MEMORY,&signature,&message);Refund();return false;
 }
 catch(...){RememberFailure("Precommit graph provider threw");android13::BorrowedPlatformString signature("OWNED BATCH"),message("Precommit graph provider threw");failure(driver,JVMTI_ERROR_INTERNAL,&signature,&message);Refund();return false;}
}
bool RejectBoundary(void* driver,const char* message){
 RememberFailure(message);
 __android_log_print(ANDROID_LOG_ERROR,"artdcevm","Owned graph boundary refused: %s",message);
 android13::BorrowedPlatformString signature("OWNED BATCH"),text(message);
 failure(driver,JVMTI_ERROR_NOT_AVAILABLE,&signature,&text);Refund();return false;
}
// Both named leaf arguments are pointers: verification is const Iterator&,
// collection is Iterator*. No value-aggregate ABI or wrapper execution assumed.
bool LeafBoundary(void* owner,const transaction_root::Iterator* iterator,bool collection,
                  void** driver,transaction_root::Holder** raw){
 auto& request=*active;
 for(const auto& item:request.items)if(item.validated_owner&&Mapped(item.validated_owner,sizeof(NativeDefinition))){*driver=Read<NativeDefinition>(item.validated_owner,0).driver;break;}
 if(!*driver)return false;
 if(!Mapped(owner,sizeof(NativeDefinition))||Read<NativeDefinition>(owner,0).driver!=*driver||!Mapped(iterator,sizeof(*iterator)))return RejectBoundary(*driver,"Unmapped source boundary owner/iterator");
 const auto current=Read<transaction_root::Iterator>(iterator,0);
 if(!Mapped(current.holder,sizeof(*current.holder)))return RejectBoundary(*driver,"Unmapped source boundary Holder");
 transaction_root::Holder holder{};size_t extent=0;
 method_identity::ReadableMappingExtent(current.holder,&extent);
 transaction_root::Metadata metadata{};
 if(!transaction_root::ReadHolderHeader(current.holder,extent,&holder)||
    !Mapped(holder.redefinitions,sizeof(transaction_root::Owners))||
    !transaction_root::ReadMetadata(holder,request.items.size(),transaction_root::OwnerFamily::kAndroid15,&metadata))
  return RejectBoundary(*driver,"Invalid source boundary Holder metadata");
 const size_t next=collection?request.collected_count:request.verified_count;
 if(current.index<0||size_t(current.index)!=next||next>=metadata.count||
    metadata.owners.begin+next*metadata.stride!=owner||
    std::count_if(request.items.begin(),request.items.end(),[&](const auto& item){return item.validated_owner==owner;})!=1||
    (request.boundary_holder&&request.boundary_holder!=current.holder))
  return RejectBoundary(*driver,"Source boundary differs from complete owned Holder order");
 for(const auto& item:request.items)if(!item.validated_owner||Read<NativeDefinition>(item.validated_owner,0).driver!=*driver)
  return RejectBoundary(*driver,"Source boundary has an unvalidated batch owner");
 request.boundary_holder=current.holder;*raw=current.holder;return true;
}
bool VerificationGraph(void* owner,const transaction_root::Iterator* iterator){
 if(!active||!active->provider->closed_graph)return verification_leaf_original(owner,iterator);
 Phase("Class verification leaf");void* driver=nullptr;transaction_root::Holder* holder=nullptr;
 if(!LeafBoundary(owner,iterator,false,&driver,&holder))return false;
 if(active->graph_verified||active->graph_sealed)return RejectBoundary(driver,"Repeated source verification leaf");
 if(Lifecycle(*active->provider)){
  if(!active->source_run_active||active->source_driver!=driver||active->verification_owner)
   return RejectBoundary(driver,"Verification lacks exact original Run lifetime");
  active->verification_owner=owner;active->verification_iterator=iterator;
  std::string error;bool allowed=false;
  try{allowed=active->provider->before_verification(owner,iterator,active->provider->state,&error);}
  catch(const std::bad_alloc&){CloseVerification(false);RememberFailure("Verification preparation exhausted native memory");Refund();android13::BorrowedPlatformString sig("OWNED BATCH"),msg("Verification preparation exhausted native memory");failure(driver,JVMTI_ERROR_OUT_OF_MEMORY,&sig,&msg);return false;}
  catch(...){CloseVerification(false);return RejectBoundary(driver,"Verification preparation threw before original leaf");}
  if(!allowed){CloseVerification(false);return RejectBoundary(driver,error.empty()?"Verification view refused":error.c_str());}
 }
 const bool original_result=verification_leaf_original(owner,iterator);CloseVerification(original_result);
 if(!original_result){RememberFailure("Original CheckVerification returned false");Refund();return false;}
 if(++active->verified_count!=active->items.size())return true;
 return GuardGraph(driver,holder,GraphWindow::Verified);
}
bool CollectionGraph(void* owner,transaction_root::Iterator* iterator){
 if(!active||!active->provider->closed_graph)return collection_leaf_original(owner,iterator);
 Phase("Class collection leaf");void* driver=nullptr;transaction_root::Holder* holder=nullptr;
 if(!LeafBoundary(owner,iterator,true,&driver,&holder))return false;
 // Missing/wrong earlier hooks refuse here BEFORE collection and its original
 // last bool. Update remains an invariant boundary, never the first refusal.
 if(!active->graph_verified||active->graph_sealed)return RejectBoundary(driver,"Collection lacks completed source verification witness");
 if(!collection_leaf_original(owner,iterator)){RememberFailure("Original CollectAndCreateNewInstances returned false");Refund();return false;}
 if(++active->collected_count!=active->items.size())return true;
 if(!GuardGraph(driver,holder,GraphWindow::Collected))return false;
 active->graph_sealed=true;return true;
}
bool VerifiedGraph(void* driver,transaction_root::Holder* raw){
 Phase("whole verification wrapper");if(active&&active->provider->closed_graph&&active->graph_verified)return RejectBoundary(driver,"Repeated verification boundary");
 if(!verified_original(driver,raw)){RememberFailure("Original CheckAllClassesAreVerified returned false");Refund();return false;}
 // A non-inlined wrapper may contain our leaf callbacks. Only one complete
 // proof is published; an inlined wrapper requires the last actual leaf.
 if(active&&active->provider->closed_graph&&active->graph_verified)return active->boundary_holder==raw||RejectBoundary(driver,"Wrapper/leaf verification Holder mismatch");
 if(active&&Lifecycle(*active->provider)&&active->verified_count!=active->items.size())return RejectBoundary(driver,"Missing exact verification leaf lifecycle");
 return GuardGraph(driver,raw,GraphWindow::Verified);
}
bool CollectGraph(void* driver,transaction_root::Holder* raw){
 Phase("whole collection wrapper");if(active&&active->provider->closed_graph&&(!active->graph_verified||active->graph_sealed))return RejectBoundary(driver,"Invalid final rejectable boundary");
 if(!collect_original(driver,raw)){RememberFailure("Original batch CollectAndCreateNewInstances returned false");Refund();return false;}
 if(!active||!active->provider->closed_graph)return true;
 if(active->graph_sealed)return active->boundary_holder==raw||RejectBoundary(driver,"Wrapper/leaf collection Holder mismatch");
 if(!GuardGraph(driver,raw,GraphWindow::Collected))return false;
 active->graph_sealed=true;return true;
}
void NotifyCommit(const source_commit::Plan& plan,void* owner) noexcept {
 auto* request=static_cast<Request*>(owner);
 if(request!=active||!request->graph_sealed||request->refunded||request->verification_owner)std::abort();
 if(Lifecycle(*request->provider)){
  if(!request->source_run_active||request->publication_started)std::abort();
  request->publication_started=true;request->provider->commit(plan,request->provider->state);
 }
}
jvmtiError SourceRun(void* driver){
 if(!active||!Lifecycle(*active->provider))return run_original(driver);
 auto& r=*active;if(r.source_run_active)return JVMTI_ERROR_NOT_AVAILABLE;
 r.source_driver=driver;r.source_run_active=true;std::string error;bool begun=false;
 jvmtiError result=JVMTI_ERROR_NOT_AVAILABLE;
 try{begun=r.provider->begin_run(driver,r.provider->state,&error);}
 catch(const std::bad_alloc&){result=JVMTI_ERROR_OUT_OF_MEMORY;RememberFailure("Source Session creation exhausted native memory");}
 catch(...){result=JVMTI_ERROR_INTERNAL;RememberFailure("Source Session creation threw");}
 if(begun){result=run_original(driver);
  // Every owned rejection and original RecordFailure refunds while source
  // locals still exist. EndRun must never perform a late Holder retirement.
  if(result!=JVMTI_ERROR_NONE&&!r.refunded)std::abort();
 }else{RememberFailure(error.empty()?"Source Session creation refused":error.c_str());Refund();}
 if(r.verification_owner)std::abort(); // No late closure after original Holder cleanup.
 r.provider->end_run(driver,result==JVMTI_ERROR_NONE,r.provider->state);
 r.source_run_active=false;r.source_driver=nullptr;
 // The original Run destroyed its private Holder. Keep diagnostics scalars,
 // never retain these borrowed addresses into the caller's next operation.
 r.holder=nullptr;r.boundary_holder=nullptr;
 for(auto& item:r.items){if(item.validated_owner)++r.completed_validated;item.validated_owner=nullptr;}
 // Successful source Run must have consumed all owners before dropping them.
 if(result==JVMTI_ERROR_NONE&&!r.remaining_owners.empty())std::abort();
 r.remaining_owners.clear();return result;
}
void Update(void* owner,const void* iterator){
  if(!active||std::none_of(active->items.begin(),active->items.end(),[&](const auto& item){return item.validated_owner==owner;})){
    reinterpret_cast<Commit>(commit_original)(owner,iterator);return;
  }
  transaction_root::Iterator current{};
  if(active->provider->closed_graph&&!active->graph_sealed)std::abort();
  if(active->provider->schema_evolution){
    // Named source entry accepts a reference to its fixed Iterator value. This
    // original argument is trusted as the source declaration requires; verify
    // its Holder/index against the independent Collected ownership proof.
    // Do not allocate an OS mapping scan in the irreversible final boundary.
    if(!iterator)std::abort();std::memcpy(&current,iterator,sizeof(current));
  }else{size_t extent=0;
    if(!method_identity::ReadableMappingExtent(iterator,&extent)||!transaction_root::ReadIterator(iterator,extent,&current))std::abort();
  }
  if(active->committed){
    // JNI and mutable managed roots are deliberately not revisited after heap
    // replacement. Only the fixed Holder/native owner previously proven remains.
    auto found=std::find(active->remaining_owners.begin(),active->remaining_owners.end(),std::make_pair(owner,current.index));
    if(current.holder!=active->holder||found==active->remaining_owners.end())std::abort();
    active->remaining_owners.erase(found);return;
  }
  if(active->provider->schema_evolution){
    // The refundable window prepared the complete plan and service, including
    // native ownership/order. Consume only preallocated state after moving GC.
    auto found=std::find(active->remaining_owners.begin(),active->remaining_owners.end(),std::make_pair(owner,current.index));
    if(!active->provider->finalize||current.holder!=active->holder||found==active->remaining_owners.end())std::abort();
    const source_commit::Plan* plan=nullptr;
    auto services=active->provider->finalize(current.holder,&plan,active->provider->state);
    if(!plan||!services||!source_commit::Run(*plan,*services,{active,NotifyCommit}))std::abort();
    active->remaining_owners.erase(found);active->committed=true;return;
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
    {boundaries::kVerifyWhole,reinterpret_cast<void*>(VerifiedGraph),reinterpret_cast<void**>(&verified_original)},
    {boundaries::kCollectWhole,reinterpret_cast<void*>(CollectGraph),reinterpret_cast<void**>(&collect_original)},
    {boundaries::kVerifyClass,reinterpret_cast<void*>(VerificationGraph),reinterpret_cast<void**>(&verification_leaf_original)},
    {boundaries::kCollectClass,reinterpret_cast<void*>(CollectionGraph),reinterpret_cast<void**>(&collection_leaf_original)}};
  void* run_target=hooks.FindSymbol("libopenjdkjvmti.so",boundaries::kRun);
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
  if(okay&&run_target){void* token=hooks.Install(run_target,reinterpret_cast<void*>(SourceRun),reinterpret_cast<void**>(&run_original),error);
   if(!token)okay=false;else{tokens.push_back(token);if(!run_original)std::abort();run_ready=true;}}
  if(!okay)run_ready=false;
  if(!okay)for(auto at=tokens.rbegin();at!=tokens.rend();++at){std::string rollback;if(!hooks.Remove(*at,&rollback))std::abort();}
  destroy(&stop);installed=okay;window_ready=okay;return okay;
}
bool GraphWindowReady(){return window_ready;}
void ObserveSourceFailure(void* driver,jvmtiError code) noexcept {
 if(!active||!active->source_run_active||active->source_driver!=driver||code==JVMTI_ERROR_NONE)return;
 RememberFailure("Original RecordFailure refused source Run");
 // RecordFailure may run INSIDE original CheckVerification/Finish while its
 // native monitor/view is still required. Notify pending refund, preserving
 // that dynamic extent; the normal leaf After closes it after original return.
 if(!active->refunded&&!active->publication_started){active->refunded=true;active->provider->refund(active->provider->state);}
}
jvmtiError RunBatch(HookEngine& hooks,JNIEnv* env,jvmtiEnv* jvmti,
    const std::vector<Definition>& definitions,const Provider& provider,std::string* error){
  if(!env||!jvmti||definitions.empty()||definitions.size()>INT32_MAX||active||!installed||!provider.ready||!provider.prepare||
     !provider.metadata||!provider.metadata->Calibrated()||!provider.ready(provider.state)||
     (definitions.size()>1&&!provider.preflight)){
    if(error)*error="Owned batch provider is not fully prepared";return JVMTI_ERROR_NOT_AVAILABLE;
  }
  if(Lifecycle(provider)&&(!CompleteLifecycle(provider)||!provider.schema_evolution||!provider.closed_graph||!run_ready)){
    if(error)*error="Source Session requires complete lifecycle and installed original Run";return JVMTI_ERROR_NOT_AVAILABLE;
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
    if(provider.schema_evolution){
      native_schema_policy::Plan schema;
      if(!provider.closed_graph||!native_schema_policy::Plan::Prepare(old,next,&schema,error))return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
      // Preserve the complete original CheckClass including its family's lock
      // semantics. Class flag evolution requires a separate guarded adapter.
      if(native_schema_policy::JavaFlags(old.access)!=native_schema_policy::JavaFlags(next.access)){
        if(error)*error="Class modifier changes require the independently proven CheckClass owner";
        return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED;
      }
    }else if(!ValidateRemoval(old,next,provider.allow_field_removal,error,false))return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;
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
  if(!removed&&!provider.schema_evolution){if(error)*error="Owned batch requires at least one actual member removal";return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED;}
  if(provider.preflight&&!provider.preflight(env,jvmti,definitions,provider.state,error))return static_cast<jvmtiError>(owned_failure_diagnostic::Unavailable(error,"owned provider input preflight"));
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
  request.remaining_owners.reserve(definitions.size());
  std::vector<jvmtiClassDefinition> incoming;incoming.reserve(definitions.size());
  for(const auto& definition:definitions)incoming.push_back({definition.klass,static_cast<jint>(definition.dex.size()),definition.dex.data()});
  const auto result=structural(jvmti,static_cast<jint>(incoming.size()),incoming.data());
  if(request.committed&&(result!=JVMTI_ERROR_NONE||!request.remaining_owners.empty()))std::abort();
  if(result==JVMTI_ERROR_NONE&&!request.committed){
    if(error)*error="Source entry did not consume every owned batch structural owner";return JVMTI_ERROR_INTERNAL;
  }
  if(result!=JVMTI_ERROR_NONE)CopyFailure(request,result,error);
  else if(error)error->clear();return result;
}
jvmtiError Run(HookEngine& hooks,JNIEnv* env,jvmtiEnv* jvmti,jclass target,
    const std::vector<unsigned char>& incoming,const Provider& provider,std::string* error){
  return RunBatch(hooks,env,jvmti,{{target,incoming}},provider,error);
}
}
