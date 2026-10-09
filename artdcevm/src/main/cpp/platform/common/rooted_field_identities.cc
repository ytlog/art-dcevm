#include "platform/common/rooted_field_identities.h"
#include <new>
namespace artdcevm::rooted_field_identities {
namespace {bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}}
struct Store::Request {Store* store;Entry* entry;};
bool Store::Observe(const runnable_jni::Context& context,void* raw){auto& r=*static_cast<Request*>(raw);const auto& b=r.store->bindings_;void *expected=nullptr,*owner=nullptr,*mirror=nullptr;
 // Public reference-kind checks in the independent oracle may transition JNI.
 // Compare each result immediately, then reread both roots with native-only
 // leaves after the final public oracle. Never carry an owner across that call.
 if(!b.oracle(b.owner,context,r.entry->owner,&expected)||!b.decode(b.owner,r.entry->owner,&owner)||expected!=owner)return false;
 if(!b.oracle(b.owner,context,r.entry->mirror,&expected)||!b.decode(b.owner,r.entry->mirror,&mirror)||expected!=mirror)return false;
 if(!b.decode(b.owner,r.entry->owner,&owner))return false;
 void* field=b.field(b.owner,mirror);if(!field||b.declaring(b.owner,field)!=owner)return false;
 r.entry->native=field;return true;
}
bool Store::Stage(JNIEnv* env,jclass owner,jfieldID id,bool statik,std::string* error){
 const auto& b=bindings_;
 if(!env||!owner||!id||env->ExceptionCheck()||!b.owner||!b.scope||!b.oracle||!b.decode||!b.field||!b.declaring)return Fail(error,"Rooted Field stage lacks actual public/native owner");
 jobject mirror=env->ToReflectedField(owner,id,statik?JNI_TRUE:JNI_FALSE);
 if(!mirror||env->ExceptionCheck())return Fail(error,"Public contextual Field round trip unavailable");
 struct Local {JNIEnv* env;jobject ref;~Local(){if(ref)env->DeleteLocalRef(ref);}} mirror_local{env,mirror};
 if(env->FromReflectedField(mirror)!=id||env->ExceptionCheck())return Fail(error,"Public contextual Field ID round trip changed");
 jclass type=env->GetObjectClass(mirror);Local type_local{env,type};
 auto declaring=type?env->GetMethodID(type,"getDeclaringClass","()Ljava/lang/Class;"):nullptr;
 auto modifiers=type?env->GetMethodID(type,"getModifiers","()I"):nullptr;
 if(!declaring||!modifiers||env->ExceptionCheck())return Fail(error,"Public contextual Field declaration unavailable");
 jobject actual=env->CallObjectMethod(mirror,declaring);Local actual_local{env,actual};
 jint flags=env->CallIntMethod(mirror,modifiers);
 if(env->ExceptionCheck()||!actual||!env->IsSameObject(actual,owner)||((flags&8)!=0)!=statik)return Fail(error,"Public Field declaring owner or storage differs from ID context");
 Entry entry;
 struct Pending {JNIEnv* env;Entry* value;~Pending(){if(value->owner)env->DeleteGlobalRef(value->owner);if(value->mirror)env->DeleteGlobalRef(value->mirror);}} pending{env,&entry};
 entry.owner=static_cast<jclass>(env->NewGlobalRef(owner));
 if(!entry.owner||env->ExceptionCheck())return false;
 entry.mirror=env->NewGlobalRef(mirror);entry.id=id;entry.is_static=statik;
 if(!entry.mirror||env->ExceptionCheck())return false;
 Request request{this,&entry};
 if(!b.scope(b.owner,env,entry.mirror,Observe,&request))return Fail(error,"Rooted native Field differs from exact public declaring Class");
 try {
 auto& bucket=entries_[id];
 for(const auto& prior:bucket){
  jobject previous=env->CallObjectMethod(prior.mirror,declaring);Local previous_local{env,previous};
  if(env->ExceptionCheck()||!previous)return Fail(error,"Staged public Field lost its declaring Class");
  if(env->IsSameObject(previous,owner)){
   const bool same=prior.native==entry.native&&prior.is_static==entry.is_static;
   return same||Fail(error,"Duplicate contextual Field ID changes native identity or storage");
  }
 }
 env->DeleteGlobalRef(entry.owner);entry.owner=nullptr;bucket.push_back(entry);entry.mirror=nullptr;return true;
 }catch(const std::bad_alloc&){
  // Even reporting may allocate after exhaustion. Preserve the refundable
  // false result and root cleanup if an error string cannot be extended.
  if(error)try{*error="Rooted Field storage allocation failed before publication";}catch(const std::bad_alloc&){}
  return false;
 }
}
void* Store::Read(jclass owner,jfieldID id)const noexcept{
 if(!owner||!id)return nullptr;const auto& b=bindings_;if(!b.decode||!b.field||!b.declaring)return nullptr;void* exact=nullptr;
 if(!b.decode(b.owner,owner,&exact)||!exact)return nullptr;
 void* result=nullptr;
 const auto found=entries_.find(id);if(found==entries_.end())return nullptr;
 for(const auto& entry:found->second){void* mirror=nullptr;
  if(!b.decode(b.owner,entry.mirror,&mirror)||!mirror)return nullptr;
  void* field=b.field(b.owner,mirror);
  if(!field||field!=entry.native)return nullptr;
  if(b.declaring(b.owner,field)!=exact)continue;
  if(result)return nullptr;result=field;
 }
 return result;
}
void Store::Clear(JNIEnv* env){for(auto& bucket:entries_)for(auto& entry:bucket.second){env->DeleteGlobalRef(entry.mirror);}entries_.clear();}
}
