#include "platform/common/native_cache_public_fields.h"
#include "platform/common/runnable_executor.h"
#include <cstring>
namespace artdcevm::native_cache_public_fields {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
struct TiBuffer {
 jvmtiEnv* ti;unsigned char* data=nullptr;
 ~TiBuffer(){if(data)ti->Deallocate(data);}
 template<class T>T* Get()const{return reinterpret_cast<T*>(data);}
};
struct Local {JNIEnv* env;jobject ref=nullptr;~Local(){if(ref)env->DeleteLocalRef(ref);}};
struct Request {
 jobject field_root,owner_root; jfieldID id;void* manager;
 void*(*field)(void*);void*(*decode)(void*,jfieldID);
 const modern_field_metadata::FieldMetadata* metadata;const char* name;
 void* native=nullptr;std::string* error;
 const source_handle_roots::Service* source_roots=nullptr;
};
bool ObserveNative(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<Request*>(raw);void* mirror=nullptr;void* owner=nullptr;
 if(!(r.source_roots?r.source_roots->BorrowOwnedStrongPair(scope,r.field_root,r.owner_root,&mirror,&owner):
    (runnable_executor::DecodeStrongGlobal(scope,r.field_root,&mirror)&&runnable_executor::DecodeStrongGlobal(scope,r.owner_root,&owner)))||!mirror||!owner)
  return Fail(r.error,"Cache Field identity roots unavailable");
 auto* native=r.field(mirror);
 if(scope.env->ExceptionCheck())return Fail(r.error,"Named reflected Field leaf left an exception");
 auto* decoded=r.decode(r.manager,r.id);
 if(scope.env->ExceptionCheck())return Fail(r.error,"Original field ID decoder left an exception");
 if(!native||decoded!=native)return Fail(r.error,"Public/native cache Field identity differs");
 field_copy::CopyFieldView text;
 if(!r.metadata->FieldIdentity(native,&text,r.error))return false;
 if(scope.env->ExceptionCheck())return Fail(r.error,"Calibrated cache Field text left an exception");
 if(!text.name||!text.descriptor||std::strcmp(text.name,r.name)||std::strcmp(text.descriptor,"J"))
  return Fail(r.error,"Calibrated cache Field name/type differs");
 auto* declaring=r.metadata->FieldDeclaringClass(native);
 if(scope.env->ExceptionCheck())return Fail(r.error,"Calibrated cache Field owner left an exception");
 if(declaring!=owner)return Fail(r.error,"Calibrated cache Field owner differs");
 r.native=native;return true;
}
}
Witness::~Witness(){
 if(reflected_)env_->DeleteGlobalRef(reflected_);
 if(owner_)env_->DeleteGlobalRef(owner_);
 if(cache_)env_->DeleteGlobalRef(cache_);
}
bool Service::Bind(HookEngine& hooks,const runtime_services::Snapshot& runtime,std::string* error){
 auto field=reinterpret_cast<Field>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 auto decode=reinterpret_cast<Decode>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID"));
 if(!field||!decode||!runtime.receivers.jni_id_manager)
  return Fail(error,"Named cache Field identity leaves/observed manager unavailable");
 field_=field;decode_=decode;manager_=runtime.receivers.jni_id_manager;return true;
}
bool Service::Observe(JNIEnv* env,jvmtiEnv* ti,jobject cache,jclass owner,const char* name,
 const modern_field_metadata::FieldMetadata& metadata,std::unique_ptr<Witness>* output,std::string* error)const{
 if(!env||!ti)return Fail(error,"Public cache Field observation lacks caller JNI/JVMTI context");
 if(!metadata.Calibrated())return Fail(error,"Public cache Field observation lacks calibrated native metadata");
 if(!cache||!owner||!name||!*name||!output||!field_||!decode_||!manager_)
  return Fail(error,"Invalid public cache Field observation");
 if(env->ExceptionCheck())return Fail(error,"Cache Field observation has a pending exception");
 TiBuffer signature{ti};auto status=ti->GetClassSignature(owner,reinterpret_cast<char**>(&signature.data),nullptr);
 if(env->ExceptionCheck())return Fail(error,"Cache Class signature left an exception");
 if(status!=JVMTI_ERROR_NONE||!signature.data||std::strcmp(signature.Get<char>(),"Ljava/lang/DexCache;"))
  return Fail(error,"Expected exact DexCache public Class descriptor");
 Local loader{env};status=ti->GetClassLoader(owner,&loader.ref);
 if(env->ExceptionCheck())return Fail(error,"Cache Class loader left an exception");
 if(status!=JVMTI_ERROR_NONE||loader.ref)return Fail(error,"Expected bootstrap DexCache namespace");
 Local actual{env,env->GetObjectClass(cache)};
 if(env->ExceptionCheck())return Fail(error,"Cache object Class left an exception");
 if(!actual.ref)return Fail(error,"Cache object Class unavailable");
 const bool same=env->IsSameObject(actual.ref,owner);
 if(env->ExceptionCheck())return Fail(error,"Cache object Class comparison left an exception");
 if(!same)return Fail(error,"Cache object's exact public Class differs");
 jint count=0;TiBuffer fields{ti};status=ti->GetClassFields(owner,&count,reinterpret_cast<jfieldID**>(&fields.data));
 if(env->ExceptionCheck())return Fail(error,"Cache declared fields left an exception");
 if(status!=JVMTI_ERROR_NONE||count<=0||!fields.data)return Fail(error,"Cache declared fields unavailable");
 jfieldID selected=nullptr;
 for(jint i=0;i<count;++i){
  const auto id=fields.Get<jfieldID>()[i];if(!id)return Fail(error,"Cache field enumeration contains a null ID");
  TiBuffer field_name{ti},type{ti};
  status=ti->GetFieldName(owner,id,reinterpret_cast<char**>(&field_name.data),reinterpret_cast<char**>(&type.data),nullptr);
  if(env->ExceptionCheck())return Fail(error,"Cache field name/type left an exception");
  if(status!=JVMTI_ERROR_NONE||!field_name.data||!type.data)return Fail(error,"Cache field name/type unavailable");
  if(std::strcmp(field_name.Get<char>(),name))continue;
  if(selected||std::strcmp(type.Get<char>(),"J"))return Fail(error,"Cache field name is duplicate or not long");
  jint flags=0;status=ti->GetFieldModifiers(owner,id,&flags);
  if(env->ExceptionCheck())return Fail(error,"Cache field modifiers left an exception");
  if(status!=JVMTI_ERROR_NONE||(flags&8))return Fail(error,"Cache field must be a declared instance long");
  Local declaring{env};status=ti->GetFieldDeclaringClass(owner,id,reinterpret_cast<jclass*>(&declaring.ref));
  if(env->ExceptionCheck())return Fail(error,"Cache field declaring Class left an exception");
  if(status!=JVMTI_ERROR_NONE||!declaring.ref)return Fail(error,"Cache field declaring Class unavailable");
  const bool same_owner=env->IsSameObject(declaring.ref,owner);
  if(env->ExceptionCheck())return Fail(error,"Cache declaring Class comparison left an exception");
  if(!same_owner)return Fail(error,"Cache field is not declared by the exact Class");
  selected=id;
 }
 if(!selected)return Fail(error,"Cache instance long field was not enumerated");
 auto witness=std::unique_ptr<Witness>(new Witness(env));witness->id_=selected;witness->name_=name;
 witness->cache_=env->NewGlobalRef(cache);
 if(env->ExceptionCheck())return Fail(error,"Cache object strong root left an exception");
 if(!witness->cache_)return Fail(error,"Cache object strong root unavailable");
 witness->owner_=static_cast<jclass>(env->NewGlobalRef(owner));
 if(env->ExceptionCheck())return Fail(error,"Cache Class strong root left an exception");
 if(!witness->owner_)return Fail(error,"Cache Class strong root unavailable");
 Local reflected{env,env->ToReflectedField(witness->owner_,selected,JNI_FALSE)};
 if(env->ExceptionCheck())return Fail(error,"Cache Field reflection left an exception");
 if(!reflected.ref)return Fail(error,"Cache Field reflection unavailable");
 const auto roundtrip=env->FromReflectedField(reflected.ref);
 if(env->ExceptionCheck())return Fail(error,"Cache Field ID roundtrip left an exception");
 if(roundtrip!=selected)return Fail(error,"Cache Field original ID roundtrip differs");
 witness->reflected_=env->NewGlobalRef(reflected.ref);
 if(env->ExceptionCheck())return Fail(error,"Cache reflected Field strong root left an exception");
 if(!witness->reflected_)return Fail(error,"Cache reflected Field strong root unavailable");
 std::string native_error;
 Request request{witness->reflected_,witness->owner_,selected,manager_,field_,decode_,&metadata,name,nullptr,&native_error};
 const auto observed=runnable_executor::With(env,witness->reflected_,ObserveNative,&request);
 if(env->ExceptionCheck())return Fail(error,"Native cache Field observation left an exception");
 if(observed!=runnable_jni::Result::kSuccess||!request.native){
  if(error)*error="Original native cache Field observation rejected name="+std::string(name)+" result="+std::to_string(static_cast<int>(observed))+" route="+runnable_executor::Route()+": "+native_error;
  return false;
 }
 witness->value_=env->GetLongField(witness->cache_,selected);
 if(env->ExceptionCheck())return Fail(error,"Original JNI cache long read left an exception");
 witness->native_=request.native;*output=std::move(witness);return true;
}
}

namespace artdcevm::native_cache_public_fields {
void Service::Release(JNIEnv* env,std::unique_ptr<Witness>* witness){
 if(env&&witness&&*witness){(*witness)->env_=env;witness->reset();}
}
bool Service::Refresh(const runnable_jni::Context& context,const source_handle_roots::Service& roots,const Witness& witness,
 const modern_field_metadata::FieldMetadata& metadata,jlong* value,std::string* error)const {
 if(!value||!context.self||context.env!=witness.env_||!context.vm||!field_||!decode_||!manager_||
    !metadata.Calibrated()||!witness.cache_||!witness.owner_||!witness.reflected_||!witness.id_)
  return Fail(error,"Live public cache Field refresh owner unavailable");
 if(context.env->ExceptionCheck())return Fail(error,"Live public cache Field refresh has pending exception");
 const auto field_kind=context.env->GetObjectRefType(witness.reflected_);
 if(context.env->ExceptionCheck())return Fail(error,"Live cache Field root kind left an exception");
 const auto owner_kind=context.env->GetObjectRefType(witness.owner_);
 if(context.env->ExceptionCheck())return Fail(error,"Live cache Class root kind left an exception");
 if(field_kind!=JNIGlobalRefType||owner_kind!=JNIGlobalRefType)return Fail(error,"Live cache Field/Class references are not retained strong globals");
 Request request{witness.reflected_,witness.owner_,witness.id_,manager_,field_,decode_,&metadata,witness.name_.c_str(),nullptr,error,&roots};
 if(!ObserveNative(context,&request))return false;
 if(request.native!=witness.native_)return Fail(error,"Live public cache Field native identity changed");
 const auto observed=context.env->GetLongField(witness.cache_,witness.id_);
 if(context.env->ExceptionCheck())return Fail(error,"Live original JNI cache long read left an exception");
 // JNI can safepoint. Reborrow reflected Field/Class roots in the caller's
 // already-owned scope, rather than nesting the process bridge recursively.
 request.native=nullptr;if(!ObserveNative(context,&request))return false;
 if(request.native!=witness.native_)return Fail(error,"Live public cache Field identity changed after JNI read");
 *value=observed;return true;
}
}
