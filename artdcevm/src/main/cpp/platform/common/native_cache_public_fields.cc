#include "platform/common/native_cache_public_fields.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/public_declared_field_mirror.h"
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
 jobject field_root,owner_root; jfieldID id;void* expected;
 void*(*field)(void*);
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
 if(!native||r.expected!=native)return Fail(r.error,"Public/native cache Field identity differs");
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
struct MirrorRequest {
 jobject mirror,owner;void*(*leaf)(void*);
 const modern_field_metadata::FieldMetadata* metadata;
 const char* name;std::string* error;
};
bool InspectMirror(const runnable_jni::Context& context,void* raw){
 auto& r=*static_cast<MirrorRequest*>(raw);void* mirror=nullptr;void* owner=nullptr;
 if(!runnable_executor::DecodeStrongGlobal(context,r.mirror,&mirror)||
    !runnable_executor::DecodeStrongGlobal(context,r.owner,&owner)||!mirror||!owner)
  return Fail(r.error,"Independent cache mirror roots unavailable");
 void* native=r.leaf(mirror);
 if(context.env->ExceptionCheck())return Fail(r.error,"Independent cache mirror native leaf pending");
 if(!native)return Fail(r.error,"Independent cache mirror native leaf unavailable");
 field_copy::CopyFieldView identity;
 if(!r.metadata->FieldIdentity(native,&identity,r.error))return false;
 if(context.env->ExceptionCheck())return Fail(r.error,"Independent cache mirror native text pending");
 if(!identity.name||std::strcmp(identity.name,r.name))return Fail(r.error,"Independent cache mirror public/native name differs");
 void* declaring=r.metadata->FieldDeclaringClass(native);
 if(context.env->ExceptionCheck())return Fail(r.error,"Independent cache mirror native owner pending");
 return declaring==owner||Fail(r.error,"Independent cache mirror public/native owner differs");
}
std::string MirrorInventory(JNIEnv* env,jclass owner,void*(*leaf)(void*),
 const modern_field_metadata::FieldMetadata& metadata){
 std::string error;jobjectArray array=nullptr;
 if(!public_declared_field_mirror::Enumerate(env,owner,&array,&error))return "unavailable: "+error;
 Local fields{env,array};const auto count=env->GetArrayLength(array);
 if(env->ExceptionCheck())return "count-pending";
 if(count<0)return "invalid-count";
 Local field_type{env,env->FindClass("java/lang/reflect/Field")};
 if(env->ExceptionCheck()||!field_type.ref)return "Field-Class-unavailable";
 const auto name=env->GetMethodID(static_cast<jclass>(field_type.ref),"getName","()Ljava/lang/String;");
 if(env->ExceptionCheck()||!name)return "name-method-unavailable";
 const auto flags=env->GetMethodID(static_cast<jclass>(field_type.ref),"getModifiers","()I");
 if(env->ExceptionCheck()||!flags)return "flags-method-unavailable";
 jobject owner_root=env->NewGlobalRef(owner);
 struct Global{JNIEnv* env;jobject ref;~Global(){if(ref)env->DeleteGlobalRef(ref);}} owner_scope{env,owner_root};
 if(env->ExceptionCheck()||!owner_root)return "owner-root-unavailable";
 std::string text;
 for(jsize i=0;i<count;++i){
  Local field{env,env->GetObjectArrayElement(array,i)};
  if(env->ExceptionCheck()||!field.ref)return "element-unavailable";
  Local public_name{env,env->CallObjectMethod(field.ref,name)};
  if(env->ExceptionCheck()||!public_name.ref)return "name-unavailable";
  const char* chars=env->GetStringUTFChars(static_cast<jstring>(public_name.ref),nullptr);
  struct Utf{JNIEnv* env;jstring string;const char* chars;~Utf(){if(chars)env->ReleaseStringUTFChars(string,chars);}} utf{env,static_cast<jstring>(public_name.ref),chars};
  if(env->ExceptionCheck()||!chars)return "name-UTF-unavailable";
  const auto modifiers=env->CallIntMethod(field.ref,flags);
  if(env->ExceptionCheck())return "modifiers-pending";
  const auto id=env->FromReflectedField(field.ref);
  if(env->ExceptionCheck()||!id)return "opaque-ID-unavailable";
  jobject mirror_root=env->NewGlobalRef(field.ref);Global mirror_scope{env,mirror_root};
  if(env->ExceptionCheck()||!mirror_root)return "mirror-root-unavailable";
  MirrorRequest request{mirror_root,owner_root,leaf,&metadata,chars,&error};
  const auto result=runnable_executor::With(env,mirror_root,InspectMirror,&request);
  if(env->ExceptionCheck())return "native-proof-pending";
  if(result!=runnable_jni::Result::kSuccess)return "native-proof-refused: "+error;
  if(text.size()<2048){
   const auto entry=(text.empty()?std::string():std::string(", "))+chars+" flags="+std::to_string(modifiers)+" nativeOwner=exact";
   text.append(entry,0,2048-text.size());
  }
 }
 return "count="+std::to_string(count)+" observed={"+text+"}";
}

}
Witness::~Witness(){
 contextual_field_id::Release(env_,&identity_);
 if(reflected_)env_->DeleteGlobalRef(reflected_);
 if(owner_)env_->DeleteGlobalRef(owner_);
 if(cache_)env_->DeleteGlobalRef(cache_);
}
bool Service::Bind(HookEngine& hooks,const runtime_services::Snapshot& runtime,std::string* error){
 auto field=reinterpret_cast<Field>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 (void)runtime;
 if(!field)return Fail(error,"Named cache Field identity leaf unavailable");
 if(!contextual_field_id::Initialize(hooks,error))return false;
 field_=field;
 original_fields_=reinterpret_cast<Fields>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9ClassUtil14GetClassFieldsEP9_jvmtiEnvP7_jclassPiPPP9_jfieldID"));
 original_name_=reinterpret_cast<Name>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9FieldUtil12GetFieldNameEP9_jvmtiEnvP7_jclassP9_jfieldIDPPcS8_S8_"));
 original_flags_=reinterpret_cast<Flags>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9FieldUtil17GetFieldModifiersEP9_jvmtiEnvP7_jclassP9_jfieldIDPi"));
 return true;
}
bool Service::Observe(JNIEnv* env,jvmtiEnv* ti,jobject cache,jclass owner,const char* name,
 const modern_field_metadata::FieldMetadata& metadata,std::unique_ptr<Witness>* output,std::string* error)const{
 if(!env||!ti)return Fail(error,"Public cache Field observation lacks caller JNI/JVMTI context");
 if(!metadata.Calibrated())return Fail(error,"Public cache Field observation lacks calibrated native metadata");
 if(!cache||!owner||!name||!*name||!output||!field_)
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
 jfieldID selected=nullptr;std::string enumerated;
 for(jint i=0;i<count;++i){
  const auto id=fields.Get<jfieldID>()[i];if(!id)return Fail(error,"Cache field enumeration contains a null ID");
  TiBuffer field_name{ti},type{ti};
  status=ti->GetFieldName(owner,id,reinterpret_cast<char**>(&field_name.data),reinterpret_cast<char**>(&type.data),nullptr);
  if(env->ExceptionCheck())return Fail(error,"Cache field name/type left an exception");
  if(status!=JVMTI_ERROR_NONE||!field_name.data||!type.data)return Fail(error,"Cache field name/type unavailable");
  jint flags=0;status=ti->GetFieldModifiers(owner,id,&flags);
  if(env->ExceptionCheck())return Fail(error,"Cache field modifiers left an exception");
  if(status!=JVMTI_ERROR_NONE)return Fail(error,"Cache field modifiers unavailable");
  if(enumerated.size()<2048){
   const auto entry=(enumerated.empty()?std::string():std::string(", "))+field_name.Get<char>()+":"+type.Get<char>()+" flags="+std::to_string(flags);
   enumerated.append(entry,0,2048-enumerated.size());
  }
  if(std::strcmp(field_name.Get<char>(),name))continue;
  if(selected||std::strcmp(type.Get<char>(),"J"))return Fail(error,"Cache field name is duplicate or not long");
  if(flags&8)return Fail(error,"Cache field must be a declared instance long");
  Local declaring{env};status=ti->GetFieldDeclaringClass(owner,id,reinterpret_cast<jclass*>(&declaring.ref));
  if(env->ExceptionCheck())return Fail(error,"Cache field declaring Class left an exception");
  if(status!=JVMTI_ERROR_NONE||!declaring.ref)return Fail(error,"Cache field declaring Class unavailable");
  const bool same_owner=env->IsSameObject(declaring.ref,owner);
  if(env->ExceptionCheck())return Fail(error,"Cache declaring Class comparison left an exception");
  if(!same_owner)return Fail(error,"Cache field is not declared by the exact Class");
  selected=id;
 }
 if(!selected){
  const auto live="Cache instance long field was not enumerated: required="+std::string(name)+" count="+std::to_string(count)+" observed={"+enumerated+"}";
  std::string original="unavailable";
  // Do not infer a sparse native declaration from the live JVMTI inventory.
  // Independently report the fixed original ART source's table and text. Even
  // if it contains the member, a live/original mismatch remains a refusal.
  if(original_fields_&&original_name_&&original_flags_){
   jint source_count=0;TiBuffer source_fields{ti};
   const auto source_status=original_fields_(ti,owner,&source_count,reinterpret_cast<jfieldID**>(&source_fields.data));
   original="count="+std::to_string(source_count)+" status="+std::to_string(source_status);
   if(env->ExceptionCheck()){if(error)*error=live+" original={"+original+" pending}";return false;}
   if(source_status==JVMTI_ERROR_NONE&&source_count>=0&&(source_fields.data||source_count==0)){
    std::string source_text;
    for(jint i=0;i<source_count;++i){
     const auto id=source_fields.Get<jfieldID>()[i];
     if(!id){source_text+=" null-ID";break;}
     TiBuffer source_name{ti},source_type{ti};
     const auto text_status=original_name_(ti,owner,id,reinterpret_cast<char**>(&source_name.data),reinterpret_cast<char**>(&source_type.data),nullptr);
     if(env->ExceptionCheck()){if(error)*error=live+" original={"+original+" text-pending}";return false;}
     if(text_status!=JVMTI_ERROR_NONE||!source_name.data||!source_type.data){source_text+=" text-status="+std::to_string(text_status);break;}
     jint source_flags=0;const auto flag_status=original_flags_(ti,owner,id,&source_flags);
     if(env->ExceptionCheck()){if(error)*error=live+" original={"+original+" flags-pending}";return false;}
     if(flag_status!=JVMTI_ERROR_NONE){source_text+=" flags-status="+std::to_string(flag_status);break;}
     if(source_text.size()<2048){
      const auto entry=(source_text.empty()?std::string():std::string(", "))+source_name.Get<char>()+":"+source_type.Get<char>()+" flags="+std::to_string(source_flags);
      source_text.append(entry,0,2048-source_text.size());
     }
    }
    original+=" observed={"+source_text+"}";
   }
  }
  const auto mirrors=MirrorInventory(env,owner,field_,metadata);
  if(error)*error=live+" original={"+original+"} uncheckedFalse={"+mirrors+"}";
  return false;
 }
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
 witness->identity_=contextual_field_id::Prepare(env,witness->reflected_,selected,error);
 if(!witness->identity_){if(error)*error="Public contextual cache Field identity unavailable: "+*error;return false;}
 if(contextual_field_id::Id(witness->identity_)!=selected||contextual_field_id::IsStatic(witness->identity_))return Fail(error,"Public contextual cache Field ID/storage differs");
 const auto native_identity=contextual_field_id::NativeField(witness->identity_);
 if(!native_identity)return Fail(error,"Public contextual cache Field native identity unavailable");
 std::string native_error;
 Request request{witness->reflected_,witness->owner_,selected,native_identity,field_,&metadata,name,nullptr,&native_error};
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
 if(!value||!context.self||context.env!=witness.env_||!context.vm||!field_||
    !metadata.Calibrated()||!witness.cache_||!witness.owner_||!witness.reflected_||!witness.id_||!witness.identity_)
  return Fail(error,"Live public cache Field refresh owner unavailable");
 if(context.env->ExceptionCheck())return Fail(error,"Live public cache Field refresh has pending exception");
 const auto field_kind=context.env->GetObjectRefType(witness.reflected_);
 if(context.env->ExceptionCheck())return Fail(error,"Live cache Field root kind left an exception");
 const auto owner_kind=context.env->GetObjectRefType(witness.owner_);
 if(context.env->ExceptionCheck())return Fail(error,"Live cache Class root kind left an exception");
 if(field_kind!=JNIGlobalRefType||owner_kind!=JNIGlobalRefType)return Fail(error,"Live cache Field/Class references are not retained strong globals");
 if(contextual_field_id::Id(witness.identity_)!=witness.id_||contextual_field_id::IsStatic(witness.identity_)||contextual_field_id::NativeField(witness.identity_)!=witness.native_)return Fail(error,"Retained contextual cache Field identity differs");
 Request request{witness.reflected_,witness.owner_,witness.id_,contextual_field_id::NativeField(witness.identity_),field_,&metadata,witness.name_.c_str(),nullptr,error,&roots};
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
