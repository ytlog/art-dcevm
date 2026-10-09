#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include <jni.h>
#include <vector>
#include <string>
#include <new>
namespace artdcevm::bounded_managed_roots {
struct Slot {size_t chunk=SIZE_MAX;jsize index=0;explicit operator bool()const{return chunk!=SIZE_MAX;}};
struct Bindings {
 void* owner=nullptr;
 bool (*scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
 bool (*oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 void* (*element)(void*,void*,size_t)=nullptr;
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
 bool Stage(JNIEnv* env,jobject object,Slot* out,std::string* error){
  if(!env||!object||!out||env->ExceptionCheck()||!bindings_.scope||!bindings_.oracle||!bindings_.decode||!bindings_.element)return Fail(error,"Shared managed root lacks calibrated public/native owner");
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
   if(pass)pass=bindings_.scope(bindings_.owner,env,object_root.ref,Observe,&request);
   if(!pass){jthrowable pending=env->ExceptionCheck()?env->ExceptionOccurred():nullptr;if(pending)env->ExceptionClear();
    env->SetObjectArrayElement(chunk.root,slot.index,nullptr);
    if(pending){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(pending);env->DeleteLocalRef(pending);}
    if(fresh){env->DeleteGlobalRef(chunk.root);chunks_.pop_back();}return false;}
   ++chunk.used;*out=slot;return true;
  }catch(const std::bad_alloc&){return Fail(error,"Shared managed root storage unavailable");}
 }
 jobject LocalRef(JNIEnv* env,Slot slot)const {if(!Valid(slot))return nullptr;return env->GetObjectArrayElement(chunks_[slot.chunk].root,slot.index);}
 bool Same(JNIEnv* env,Slot slot,jobject value)const {if(!slot)return !value;Local local{env,LocalRef(env,slot)};return local.ref&&!env->ExceptionCheck()&&env->IsSameObject(local.ref,value);}
 bool Read(Slot slot,void** value)const noexcept {if(!value)return false;*value=nullptr;if(!slot)return true;if(!Valid(slot))return false;
  void* array=nullptr;if(!bindings_.decode(bindings_.owner,chunks_[slot.chunk].root,&array)||!array)return false;
  *value=bindings_.element(bindings_.owner,array,size_t(slot.index));return *value!=nullptr;}
 void Clear(JNIEnv* env){for(auto& chunk:chunks_)env->DeleteGlobalRef(chunk.root);chunks_.clear();}
 bool Empty()const{return chunks_.empty();}
 private:
 struct Local {JNIEnv* env;jobject ref;~Local(){if(ref)env->DeleteLocalRef(ref);}};
 struct Global {JNIEnv* env;jobject ref;~Global(){if(ref)env->DeleteGlobalRef(ref);}};
 struct Chunk {jobjectArray root;jsize used;};
 struct Request {Store* store;jobject object,array;jsize index;};
 static bool Observe(const runnable_jni::Context& context,void* raw){auto& r=*static_cast<Request*>(raw);const auto& b=r.store->bindings_;
  for(auto root:{r.object,r.array}){void *expected=nullptr,*actual=nullptr;if(!b.oracle(b.owner,context,root,&expected)||!b.decode(b.owner,root,&actual)||expected!=actual)return false;}
  void *object=nullptr,*array=nullptr;return b.decode(b.owner,r.object,&object)&&b.decode(b.owner,r.array,&array)&&b.element(b.owner,array,size_t(r.index))==object;}
 bool Valid(Slot slot)const{return slot.chunk<chunks_.size()&&slot.index>=0&&slot.index<chunks_[slot.chunk].used;}
 static bool Fail(std::string* error,const char* text){if(error)try{*error=text;}catch(const std::bad_alloc&){}return false;}
 Bindings bindings_{};std::vector<Chunk> chunks_;
};
}
