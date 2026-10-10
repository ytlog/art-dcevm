#pragma once
#include <jni.h>
#include <string>
namespace artdcevm::public_declared_field_mirror {
// Class.getDeclaredFieldsUnchecked(false) uses the original source's force_resolve=false
// factory. Do not substitute JNI ToReflectedField (force_resolve=true).
inline bool Enumerate(JNIEnv* env,jclass owner,jobjectArray* out,std::string* error){
 if(out)*out=nullptr;
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!owner||!out||env->ExceptionCheck())return fail("Declared Field source enumeration invalid or pending");
 jclass type=env->GetObjectClass(owner);
 if(!type||env->ExceptionCheck()){if(type)env->DeleteLocalRef(type);return fail("Declared Field Class API unavailable");}
 auto method=env->GetMethodID(type,"getDeclaredFieldsUnchecked","(Z)[Ljava/lang/reflect/Field;");
 env->DeleteLocalRef(type);
 if(!method||env->ExceptionCheck())return fail("Declared Field original source accessor unavailable");
 auto fields=static_cast<jobjectArray>(env->CallObjectMethod(owner,method,JNI_FALSE));
 if(!fields||env->ExceptionCheck()){if(fields)env->DeleteLocalRef(fields);return fail("Declared Field original source enumeration failed");}
 *out=fields;return true;
}
class Cache {
 jclass owner_=nullptr; jobject fields_=nullptr;
 public:
 Cache()=default;Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
 void Clear(JNIEnv* env){if(fields_)env->DeleteGlobalRef(fields_);if(owner_)env->DeleteGlobalRef(owner_);fields_=nullptr;owner_=nullptr;}
 bool Create(JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject* out,std::string* error){
 if(out)*out=nullptr;
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!owner||!id||!out||env->ExceptionCheck())return fail("Declared Field source request invalid or pending");
 struct Local{JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 bool reuse=false;
 if(owner_){reuse=env->IsSameObject(owner_,owner);if(env->ExceptionCheck())return false;}
 if(!reuse){
  Clear(env);
  jobjectArray local=nullptr;if(!Enumerate(env,owner,&local,error))return false;
  Local fields{env,local};
  jobject rooted_owner=env->NewGlobalRef(owner);
  if(!rooted_owner||env->ExceptionCheck()){if(rooted_owner)env->DeleteGlobalRef(rooted_owner);return false;}
  jobject rooted_fields=env->NewGlobalRef(fields.value);
  if(!rooted_fields||env->ExceptionCheck()){if(rooted_fields)env->DeleteGlobalRef(rooted_fields);env->DeleteGlobalRef(rooted_owner);return false;}
  owner_=static_cast<jclass>(rooted_owner);fields_=rooted_fields;
 }
 const jsize count=env->GetArrayLength(static_cast<jarray>(fields_));
 if(env->ExceptionCheck())return false;
 jobject selected=nullptr;
 struct Root{JNIEnv* env;jobject& value;~Root(){if(value)env->DeleteGlobalRef(value);}} root{env,selected};
 for(jsize i=0;i<count;++i){
  Local field{env,env->GetObjectArrayElement(static_cast<jobjectArray>(fields_),i)};
  if(!field.value||env->ExceptionCheck())return fail("Declared Field array lost an original mirror");
  const auto actual=env->FromReflectedField(field.value);if(env->ExceptionCheck())return false;
  if(actual!=id)continue;
  Local field_type{env,env->GetObjectClass(field.value)};
  if(!field_type.value||env->ExceptionCheck())return false;
  auto get_owner=env->GetMethodID(static_cast<jclass>(field_type.value),"getDeclaringClass","()Ljava/lang/Class;");
  if(!get_owner||env->ExceptionCheck())return false;
  auto get_flags=env->GetMethodID(static_cast<jclass>(field_type.value),"getModifiers","()I");
  if(!get_flags||env->ExceptionCheck())return false;
  Local declaring{env,env->CallObjectMethod(field.value,get_owner)};
  if(!declaring.value||env->ExceptionCheck())return false;
  const bool same=env->IsSameObject(declaring.value,owner);if(env->ExceptionCheck())return false;
  const jint flags=env->CallIntMethod(field.value,get_flags);if(env->ExceptionCheck())return false;
  if(!same||((flags&8)!=0)!=statik||selected)return fail("Declared Field ID namespace/storage is inconsistent or ambiguous");
  selected=env->NewGlobalRef(field.value);if(!selected||env->ExceptionCheck())return false;
 }
 if(!selected)return fail("Declared Field source does not contain requested opaque ID");
 *out=selected;selected=nullptr;return true;
}
};
inline bool Create(JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject* out,std::string* error){
 Cache cache;const bool ok=cache.Create(env,owner,id,statik,out,error);cache.Clear(env);return ok;
}
}
