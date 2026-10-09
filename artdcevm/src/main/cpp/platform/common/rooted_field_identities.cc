#include "platform/common/rooted_field_identities.h"
#include <new>
namespace artdcevm::rooted_field_identities {
namespace {bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct Local {JNIEnv* env;jobject ref;~Local(){if(ref)env->DeleteLocalRef(ref);}};
struct Global {JNIEnv* env;jobject ref=nullptr;~Global(){if(ref)env->DeleteGlobalRef(ref);}};
}
struct Store::Request {Store* store;jobject owner,mirror,array=nullptr;jsize slot=0;void* native=nullptr;};
bool Store::Observe(const runnable_jni::Context& context,void* raw){auto& r=*static_cast<Request*>(raw);const auto& b=r.store->bindings_;
 const auto prove=[&](jobject root){void *expected=nullptr,*actual=nullptr;return b.oracle(b.owner,context,root,&expected)&&b.decode(b.owner,root,&actual)&&expected==actual;};
 if(!prove(r.owner)||!prove(r.mirror)||(r.array&&!prove(r.array)))return false;
 // Public oracles may transition JNI. Reread all roots after their final call;
 // no borrowed Class, Field or Array crosses another public/private safepoint.
 void *owner=nullptr,*mirror=nullptr;
 if(!b.decode(b.owner,r.owner,&owner)||!b.decode(b.owner,r.mirror,&mirror))return false;
 if(r.array){void* array=nullptr;if(!b.decode(b.owner,r.array,&array)||b.element(b.owner,array,size_t(r.slot))!=mirror)return false;}
 void* field=b.field(b.owner,mirror);if(!field||b.declaring(b.owner,field)!=owner)return false;
 if(r.native&&r.native!=field)return false;r.native=field;return true;
}
bool Store::Stage(JNIEnv* env,jclass owner,jfieldID id,bool statik,std::string* error){
 const auto& b=bindings_;
 if(!env||!owner||!id||env->ExceptionCheck()||!b.owner||!b.scope||!b.oracle||!b.decode||!b.field||!b.declaring||!b.element)return Fail(error,"Rooted Field stage lacks actual public/native owner");
 jobject mirror=env->ToReflectedField(owner,id,statik?JNI_TRUE:JNI_FALSE);Local mirror_local{env,mirror};
 if(!mirror||env->ExceptionCheck())return Fail(error,"Public contextual Field round trip unavailable");
 if(env->FromReflectedField(mirror)!=id||env->ExceptionCheck())return Fail(error,"Public contextual Field ID round trip changed");
 jclass type=env->GetObjectClass(mirror);Local type_local{env,type};
 auto declaring=type?env->GetMethodID(type,"getDeclaringClass","()Ljava/lang/Class;"):nullptr;
 auto modifiers=type?env->GetMethodID(type,"getModifiers","()I"):nullptr;
 if(!declaring||!modifiers||env->ExceptionCheck())return Fail(error,"Public contextual Field declaration unavailable");
 jobject actual=env->CallObjectMethod(mirror,declaring);Local actual_local{env,actual};
 jint flags=env->CallIntMethod(mirror,modifiers);
 if(env->ExceptionCheck()||!actual||!env->IsSameObject(actual,owner)||((flags&8)!=0)!=statik)return Fail(error,"Public Field declaring owner or storage differs from ID context");
 Global owner_root{env,env->NewGlobalRef(owner)};if(!owner_root.ref||env->ExceptionCheck())return false;
 Global mirror_root{env,env->NewGlobalRef(mirror)};if(!mirror_root.ref||env->ExceptionCheck())return false;
 Request request{this,owner_root.ref,mirror_root.ref};
 if(!b.scope(b.owner,env,mirror_root.ref,Observe,&request))return Fail(error,"Rooted native Field differs from exact public declaring Class");
 try {
  auto& bucket=entries_[id];
  for(const auto& prior:bucket){
   if(prior.chunk>=chunks_.size()||prior.slot<0||prior.slot>=chunks_[prior.chunk].used)return false;
   jobject previous_mirror=env->GetObjectArrayElement(chunks_[prior.chunk].root,prior.slot);Local previous_mirror_local{env,previous_mirror};
   if(!previous_mirror||env->ExceptionCheck())return Fail(error,"Shared Field root lost a staged mirror");
   jobject previous=env->CallObjectMethod(previous_mirror,declaring);Local previous_local{env,previous};
   if(env->ExceptionCheck()||!previous)return Fail(error,"Staged public Field lost its declaring Class");
   if(env->IsSameObject(previous,owner))return (prior.native==request.native&&prior.is_static==statik)||Fail(error,"Duplicate contextual Field ID changes native identity or storage");
  }
  // Reserve all C++ publication storage BEFORE installing the shared heap root.
  bucket.reserve(bucket.size()+1);
  const bool fresh=chunks_.empty()||chunks_.back().used==kChunkSize;
  if(fresh){
   chunks_.reserve(chunks_.size()+1);
   jobjectArray array=env->NewObjectArray(kChunkSize,type,nullptr);Local array_local{env,array};
   if(!array||env->ExceptionCheck()||env->GetArrayLength(array)!=kChunkSize)return Fail(error,"Shared Field array creation failed");
   Global array_root{env,env->NewGlobalRef(array)};if(!array_root.ref||env->ExceptionCheck())return false;
   chunks_.push_back({static_cast<jobjectArray>(array_root.ref),0});array_root.ref=nullptr;
  }
  const size_t index=chunks_.size()-1;auto& chunk=chunks_.back();const jsize slot=chunk.used;
  struct PendingSlot {Store* store;JNIEnv* env;size_t chunk;jsize slot;bool fresh,active=true;
   ~PendingSlot(){if(!active)return;auto& holder=store->chunks_[chunk];
    jthrowable pending=env->ExceptionCheck()?env->ExceptionOccurred():nullptr;
    if(pending)env->ExceptionClear();env->SetObjectArrayElement(holder.root,slot,nullptr);
    if(pending){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(pending);env->DeleteLocalRef(pending);}
    if(fresh&&holder.used==0){env->DeleteGlobalRef(holder.root);store->chunks_.pop_back();}
   }
  } pending{this,env,index,slot,fresh};
  env->SetObjectArrayElement(chunk.root,slot,mirror);if(env->ExceptionCheck())return false;
  jobject returned=env->GetObjectArrayElement(chunk.root,slot);Local returned_local{env,returned};
  if(!returned||env->ExceptionCheck()||!env->IsSameObject(returned,mirror))return Fail(error,"Shared Field array slot differs from public mirror");
  request.array=chunk.root;request.slot=slot;
  if(!b.scope(b.owner,env,mirror_root.ref,Observe,&request))return Fail(error,"Native shared Field slot differs from rooted public mirror");
  bucket.push_back({id,request.native,statik,index,slot});++chunk.used;pending.active=false;return true;
 }catch(const std::bad_alloc&){
  if(error)try{*error="Rooted Field storage allocation failed before publication";}catch(const std::bad_alloc&){}
  return false;
 }
}
void* Store::Read(jclass owner,jfieldID id)const noexcept{
 if(!owner||!id)return nullptr;const auto& b=bindings_;if(!b.decode||!b.field||!b.declaring||!b.element)return nullptr;void* exact=nullptr;
 if(!b.decode(b.owner,owner,&exact)||!exact)return nullptr;
 void* result=nullptr;const auto found=entries_.find(id);if(found==entries_.end())return nullptr;
 for(const auto& entry:found->second){if(entry.chunk>=chunks_.size())return nullptr;const auto& chunk=chunks_[entry.chunk];
  if(entry.slot<0||entry.slot>=chunk.used)return nullptr;void* array=nullptr;
  if(!b.decode(b.owner,chunk.root,&array)||!array)return nullptr;
  void* mirror=b.element(b.owner,array,size_t(entry.slot));if(!mirror)return nullptr;
  void* field=b.field(b.owner,mirror);if(!field||field!=entry.native)return nullptr;
  if(b.declaring(b.owner,field)!=exact)continue;if(result)return nullptr;result=field;
 }
 return result;
}
void Store::Clear(JNIEnv* env){entries_.clear();for(auto& chunk:chunks_)env->DeleteGlobalRef(chunk.root);chunks_.clear();}
}
