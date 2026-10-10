#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <jni.h>
#include <vector>
#include <string>
#include <new>
#include <atomic>
namespace artdcevm::bounded_managed_roots {
struct Slot {size_t chunk=SIZE_MAX;jsize index=0;explicit operator bool()const{return chunk!=SIZE_MAX;}};
struct Bindings {
 void* owner=nullptr;
 bool (*scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 void* (*element)(void*,void*,size_t)=nullptr;
 // Optional typed pair borrow verifies both native identities in this single
 // post-JNI observation; the store still checks its independent slot barrier.
 bool (*borrow_pair)(void*,const runnable_jni::Context&,jobject,jobject,void**,void**)=nullptr;
};
// Object[] retains every Class/loader/ancestor/interface, including repeated
// identities. JNI table consumption grows by one global per 1024 references.
// Every native slot is attested against public JNI before publication. Reads
// use the existing calibrated barrier, with no JNI/allocation in final windows.
class Store {
 public:
 static constexpr jsize kChunkSize=1024;
 Store()=default;
 explicit Store(Bindings b):bindings_(b){}
 Store(const Store&)=delete;Store& operator=(const Store&)=delete;
 Store(Store&&)=default;Store& operator=(Store&&)=default;
 void Bind(Bindings b){bindings_=b;}
 bool Stage(JNIEnv* env,jobject object,Slot* out,std::string* error){return StageImpl(env,object,out,error,nullptr);}
 // One ordinary-JNI cohort retains every original identity in an independent
 // temporary array, and publishes only after one native-only slot proof.
 // The bound limits temporary roots, never the full process inventory.
 static constexpr size_t kCohortSize=128;
 bool StageMany(JNIEnv* env,const std::vector<jobject>& objects,std::vector<Slot>* out,std::string* error){
  if(!env||!out||objects.empty()||objects.size()>kCohortSize||env->ExceptionCheck()||!bindings_.scope||!bindings_.oracle||!bindings_.decode||!bindings_.element)
   return Fail(error,"Managed root cohort lacks actual public/native owner");
  for(auto object:objects)if(!object)return Fail(error,"Managed root cohort contains null identity");
  Local type{env,env->FindClass("java/lang/Object")};if(!type.ref||env->ExceptionCheck())return false;
  Local original{env,env->NewObjectArray(jsize(objects.size()),static_cast<jclass>(type.ref),nullptr)};
  if(!original.ref||env->ExceptionCheck())return false;
  Global source{env,env->NewGlobalRef(original.ref)};if(!source.ref||env->ExceptionCheck())return false;
  const size_t old_chunks=chunks_.size();const jsize old_used=old_chunks?chunks_.back().used:0;
  std::vector<Slot> slots;
  struct Rollback {Store& store;JNIEnv* env;size_t old_chunks;jsize old_used;std::vector<Slot>& slots;bool committed=false;
   ~Rollback(){if(committed)return;
    jthrowable pending=env->ExceptionCheck()?env->ExceptionOccurred():nullptr;if(pending)env->ExceptionClear();
    for(auto slot:slots)if(slot.chunk<old_chunks)env->SetObjectArrayElement(store.chunks_[slot.chunk].root,slot.index,nullptr);
    while(store.chunks_.size()>old_chunks){env->DeleteGlobalRef(store.chunks_.back().root);store.chunks_.pop_back();}
    if(old_chunks)store.chunks_.back().used=old_used;
    if(pending){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(pending);env->DeleteLocalRef(pending);}
   }
  } rollback{*this,env,old_chunks,old_used,slots};
  try {
   slots.reserve(objects.size());chunks_.reserve(old_chunks+2);
   for(size_t i=0;i<objects.size();++i){
    if(chunks_.empty()||chunks_.back().used==kChunkSize){
     Local array{env,env->NewObjectArray(kChunkSize,static_cast<jclass>(type.ref),nullptr)};
     if(!array.ref||env->ExceptionCheck()||env->GetArrayLength(static_cast<jarray>(array.ref))!=kChunkSize)return false;
     Global root{env,env->NewGlobalRef(array.ref)};if(!root.ref||env->ExceptionCheck())return false;
     chunks_.push_back({static_cast<jobjectArray>(root.ref),0});root.ref=nullptr;
    }
    auto& chunk=chunks_.back();Slot slot{chunks_.size()-1,chunk.used};slots.push_back(slot);
    env->SetObjectArrayElement(static_cast<jobjectArray>(original.ref),jsize(i),objects[i]);if(env->ExceptionCheck())return false;
    env->SetObjectArrayElement(chunk.root,slot.index,objects[i]);if(env->ExceptionCheck())return false;
    Local a{env,env->GetObjectArrayElement(static_cast<jobjectArray>(original.ref),jsize(i))};if(!a.ref||env->ExceptionCheck())return false;
    Local b{env,env->GetObjectArrayElement(chunk.root,slot.index)};if(!b.ref||env->ExceptionCheck()||!env->IsSameObject(a.ref,objects[i])||!env->IsSameObject(a.ref,b.ref))return false;
    ++chunk.used;
   }
   Cohort request{this,source.ref,&slots};
   if(!bindings_.scope(bindings_.owner,env,source.ref,ObserveMany,&request))return Fail(error,"Managed root cohort differs from independent public slots");
   *out=std::move(slots);rollback.committed=true;return true;
  }catch(const std::bad_alloc&){return Fail(error,"Managed root cohort publication exhausted native memory");}
 }
 // An independently proven original source Runnable context needs no nested
 // runnable executor. Public JNI and the same oracle still attest each slot.
 bool Stage(const runnable_jni::Context& context,jobject object,Slot* out,std::string* error){return StageImpl(context.env,object,out,error,&context);}
 private:
 bool StageImpl(JNIEnv* env,jobject object,Slot* out,std::string* error,const runnable_jni::Context* context){
  if(!env||!object||!out||env->ExceptionCheck()||(!context&&!bindings_.scope)||!bindings_.oracle||!bindings_.decode||!bindings_.element)return Fail(error,"Shared managed root lacks calibrated public/native owner");
  Local type{env,env->FindClass("java/lang/Object")};if(!type.ref||env->ExceptionCheck())return false;
  Global object_root{env,env->NewGlobalRef(object)};if(!object_root.ref||env->ExceptionCheck())return false;
  try {
   const bool fresh=chunks_.empty()||chunks_.back().used==kChunkSize;
   if(fresh){chunks_.reserve(chunks_.size()+1);Local local{env,env->NewObjectArray(kChunkSize,static_cast<jclass>(type.ref),nullptr)};
    if(!local.ref||env->ExceptionCheck()||env->GetArrayLength(static_cast<jarray>(local.ref))!=kChunkSize)return false;
    Global root{env,env->NewGlobalRef(local.ref)};if(!root.ref||env->ExceptionCheck())return false;
    chunks_.push_back({static_cast<jobjectArray>(root.ref),0});root.ref=nullptr;}
   auto& chunk=chunks_.back();Slot slot{chunks_.size()-1,chunk.used};
   env->SetObjectArrayElement(chunk.root,slot.index,object);
   bool pass=!env->ExceptionCheck();
   if(pass){Local returned{env,env->GetObjectArrayElement(chunk.root,slot.index)};pass=returned.ref&&!env->ExceptionCheck()&&env->IsSameObject(object,returned.ref);}
   Request request{this,object_root.ref,chunk.root,slot.index};
   if(pass)pass=context?Observe(*context,&request):bindings_.scope(bindings_.owner,env,object_root.ref,Observe,&request);
   if(!pass){jthrowable pending=env->ExceptionCheck()?env->ExceptionOccurred():nullptr;if(pending)env->ExceptionClear();
    env->SetObjectArrayElement(chunk.root,slot.index,nullptr);
    if(pending){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(pending);env->DeleteLocalRef(pending);}
    if(fresh){env->DeleteGlobalRef(chunk.root);chunks_.pop_back();}return false;}
   ++chunk.used;*out=slot;return true;
  }catch(const std::bad_alloc&){return Fail(error,"Shared managed root storage unavailable");}
 }
 public:
 uint64_t Generation()const noexcept{return generation_;}
 bool Valid(Slot slot)const noexcept{return slot.chunk<chunks_.size()&&slot.index>=0&&slot.index<chunks_[slot.chunk].used;}
 jobject LocalRef(JNIEnv* env,Slot slot)const {if(!Valid(slot))return nullptr;return env->GetObjectArrayElement(chunks_[slot.chunk].root,slot.index);}
 bool Same(JNIEnv* env,Slot slot,jobject value)const {if(!slot)return !value;Local local{env,LocalRef(env,slot)};return local.ref&&!env->ExceptionCheck()&&env->IsSameObject(local.ref,value);}
 bool Read(Slot slot,void** value)const noexcept {if(!value)return false;*value=nullptr;if(!slot)return true;if(!Valid(slot))return false;
  void* array=nullptr;if(!bindings_.decode(bindings_.owner,chunks_[slot.chunk].root,&array)||!array)return false;
  *value=bindings_.element(bindings_.owner,array,size_t(slot.index));return *value!=nullptr;}
 void Clear(JNIEnv* env){for(auto& chunk:chunks_)env->DeleteGlobalRef(chunk.root);chunks_.clear();generation_=NextGeneration();}
 bool Empty()const{return chunks_.empty();}
 private:
 struct Local {JNIEnv* env;jobject ref;~Local(){if(ref)env->DeleteLocalRef(ref);}};
 struct Global {JNIEnv* env;jobject ref;~Global(){if(ref)env->DeleteGlobalRef(ref);}};
 struct Chunk {jobjectArray root;jsize used;};
 struct Cohort {Store* store;jobject source;const std::vector<Slot>* slots;};
 static bool ObserveMany(const runnable_jni::Context& context,void* raw){
  auto& r=*static_cast<Cohort*>(raw);const auto& b=r.store->bindings_;
  const auto prove=[&](jobject root){void* expected=nullptr;void* actual=nullptr;return b.oracle(b.owner,context,root,&expected)&&expected&&b.decode(b.owner,root,&actual)&&actual==expected;};
  if(!prove(r.source))return false;
  size_t previous=SIZE_MAX;
  for(auto slot:*r.slots){if(!r.store->Valid(slot))return false;if(slot.chunk!=previous){if(!prove(r.store->chunks_[slot.chunk].root))return false;previous=slot.chunk;}}
  // All public/native oracle calls are complete. Freshly borrow array roots;
  // no JNI, allocation or callback capable of safepoint occurs in this loop.
  void* source=nullptr;if(!b.decode(b.owner,r.source,&source)||!source)return false;
  previous=SIZE_MAX;void* retained=nullptr;
  for(size_t i=0;i<r.slots->size();++i){auto slot=(*r.slots)[i];
   if(slot.chunk!=previous){if(!b.decode(b.owner,r.store->chunks_[slot.chunk].root,&retained)||!retained)return false;previous=slot.chunk;}
   void* original=b.element(b.owner,source,i);if(!original||b.element(b.owner,retained,size_t(slot.index))!=original)return false;
  }return true;
 }
 struct Request {Store* store;jobject object,array;jsize index;};
 static bool Observe(const runnable_jni::Context& context,void* raw){auto& r=*static_cast<Request*>(raw);const auto& b=r.store->bindings_;
  if(b.borrow_pair){void*object=nullptr,*array=nullptr;return b.borrow_pair(b.owner,context,r.object,r.array,&object,&array)&&object&&array&&b.element(b.owner,array,size_t(r.index))==object;}
  for(auto root:{r.object,r.array}){void *expected=nullptr,*actual=nullptr;if(!b.oracle(b.owner,context,root,&expected)||!b.decode(b.owner,root,&actual)||expected!=actual)return false;}
  void *object=nullptr,*array=nullptr;return b.decode(b.owner,r.object,&object)&&b.decode(b.owner,r.array,&array)&&b.element(b.owner,array,size_t(r.index))==object;}
 static bool Fail(std::string* error,const char* text){if(error)try{*error=text;}catch(const std::bad_alloc&){}return false;}
 static uint64_t NextGeneration()noexcept{static std::atomic<uint64_t> next{1};return next.fetch_add(1,std::memory_order_relaxed);}
 uint64_t generation_=NextGeneration();Bindings bindings_{};std::vector<Chunk> chunks_;
};
}
