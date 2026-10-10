#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <new>
namespace artdcevm::public_declared_field_mirror {
// Fixed AOSP Class native declaration uses force_resolve=false. Public no-arg
// getDeclaredFields instead resolves types and cannot replace this capability.
class Accessor {
 jclass klass_=nullptr;jmethodID method_=nullptr;
 public:
 Accessor()=default;Accessor(const Accessor&)=delete;Accessor& operator=(const Accessor&)=delete;
 void Clear(JNIEnv* env){if(klass_)env->DeleteGlobalRef(klass_);klass_=nullptr;method_=nullptr;}
 bool Bind(JNIEnv* env,jvmtiEnv* ti,std::string* error){
  const auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!env||!ti||env->ExceptionCheck())return fail("Declared Field source method context invalid or pending");
  if(method_)return true;
  struct Local{JNIEnv* env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
  struct Buffer{jvmtiEnv* ti;unsigned char* value=nullptr;~Buffer(){if(value)ti->Deallocate(value);}};
  Local type{env,env->FindClass("java/lang/Class")};
  if(!type.value||env->ExceptionCheck())return fail("Declared Field bootstrap Class unavailable");
  auto klass=static_cast<jclass>(type.value);Buffer signature{ti};
  auto result=ti->GetClassSignature(klass,reinterpret_cast<char**>(&signature.value),nullptr);
  if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||!signature.value||std::strcmp(reinterpret_cast<char*>(signature.value),"Ljava/lang/Class;"))return fail("Declared Field source requires exact java/lang/Class");
  Local loader{env};result=ti->GetClassLoader(klass,&loader.value);
  if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||loader.value)return fail("Declared Field source Class must be bootstrap-defined");
  jint count=0;Buffer methods{ti};result=ti->GetClassMethods(klass,&count,reinterpret_cast<jmethodID**>(&methods.value));
  if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||count<0||(count&&!methods.value))return fail("Declared Field source original method inventory unavailable");
  jmethodID selected=nullptr;const auto* ids=reinterpret_cast<const jmethodID*>(methods.value);
  for(jint n=0;n<count;++n){Buffer name{ti},descriptor{ti};
   if(!ids[n])return fail("Declared Field source inventory contains null method ID");
   result=ti->GetMethodName(ids[n],reinterpret_cast<char**>(&name.value),reinterpret_cast<char**>(&descriptor.value),nullptr);
   if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||!name.value||!descriptor.value)return fail("Declared Field source original method declaration unavailable");
   if(std::strcmp(reinterpret_cast<char*>(name.value),"getDeclaredFieldsUnchecked")||std::strcmp(reinterpret_cast<char*>(descriptor.value),"(Z)[Ljava/lang/reflect/Field;"))continue;
   jint modifiers=0;result=ti->GetMethodModifiers(ids[n],&modifiers);
   if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||(modifiers&8)||!(modifiers&0x100))return fail("Declared Field source accessor must be original instance native method");
   Local declaring{env};result=ti->GetMethodDeclaringClass(ids[n],reinterpret_cast<jclass*>(&declaring.value));
   if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||!declaring.value)return fail("Declared Field source accessor declaring Class unavailable");
   const bool same=env->IsSameObject(declaring.value,klass);if(env->ExceptionCheck())return false;
   if(!same||selected)return fail("Declared Field source original accessor owner or multiplicity differs");selected=ids[n];
  }
  if(!selected)return fail("Declared Field original false-resolve native accessor unavailable");
  auto root=static_cast<jclass>(env->NewGlobalRef(klass));
  if(!root||env->ExceptionCheck()){if(root)env->DeleteGlobalRef(root);return false;}
  klass_=root;method_=selected;return true;
 }
 bool Enumerate(JNIEnv* env,jclass owner,jobjectArray* out,std::string* error){
  if(out)*out=nullptr;
  const auto fail=[&](const char* text){if(error)*error=text;return false;};
  if(!env||!owner||!out||!klass_||!method_||env->ExceptionCheck())return fail("Declared Field original source is unbound or pending");
  auto type=env->GetObjectClass(owner);
  if(!type||env->ExceptionCheck()){if(type)env->DeleteLocalRef(type);return fail("Declared Field owner Class identity unavailable");}
  const bool same=env->IsSameObject(type,klass_);env->DeleteLocalRef(type);
  if(env->ExceptionCheck())return false;if(!same)return fail("Declared Field owner is outside original bootstrap Class namespace");
  auto fields=static_cast<jobjectArray>(env->CallObjectMethod(owner,method_,JNI_FALSE));
  if(!fields||env->ExceptionCheck()){if(fields)env->DeleteLocalRef(fields);return fail("Declared Field original source enumeration failed");}
  *out=fields;return true;
 }
};
inline bool Enumerate(JNIEnv* env,jvmtiEnv* ti,jclass owner,jobjectArray* out,std::string* error){
 if(out)*out=nullptr;Accessor accessor;
 if(!accessor.Bind(env,ti,error))return false;
 const bool result=accessor.Enumerate(env,owner,out,error);accessor.Clear(env);return result;
}
class Cache {
 Accessor accessor_;
 jclass owner_=nullptr; jobject fields_=nullptr;
 // Opaque IDs and ordinals only: Field/Class addresses must be decoded anew.
 std::unordered_map<jfieldID,jsize> ordinals_;
 public:
 Cache()=default;Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
 bool Bind(JNIEnv* env,jvmtiEnv* ti,std::string* error){return accessor_.Bind(env,ti,error);}
 void Clear(JNIEnv* env){accessor_.Clear(env);ClearFields(env);}
 private:
 void ClearFields(JNIEnv* env){if(fields_)env->DeleteGlobalRef(fields_);if(owner_)env->DeleteGlobalRef(owner_);fields_=nullptr;owner_=nullptr;ordinals_.clear();}
 public:
 bool Create(JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject* out,std::string* error){
 if(out)*out=nullptr;
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!owner||!id||!out||env->ExceptionCheck())return fail("Declared Field source request invalid or pending");
 struct Local{JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 bool reuse=false;
 if(owner_){reuse=env->IsSameObject(owner_,owner);if(env->ExceptionCheck())return false;}
 if(!reuse){
  ClearFields(env);
  jobjectArray local=nullptr;if(!accessor_.Enumerate(env,owner,&local,error))return false;
  Local fields{env,local};
  const jsize count=env->GetArrayLength(local);if(env->ExceptionCheck())return false;
  if(count<0)return fail("Declared Field source has invalid length");
  std::unordered_map<jfieldID,jsize> index;
  try {
   index.reserve(size_t(count));
   for(jsize i=0;i<count;++i){
    Local field{env,env->GetObjectArrayElement(local,i)};
    if(!field.value||env->ExceptionCheck())return fail("Declared Field array lost an original mirror");
    const auto actual=env->FromReflectedField(field.value);if(env->ExceptionCheck())return false;
    if(!actual||!index.emplace(actual,i).second)return fail("Declared Field ID namespace/storage is inconsistent or ambiguous");
   }
  }catch(const std::bad_alloc&){return fail("Declared Field ordinal storage allocation failed");}
  jobject rooted_owner=env->NewGlobalRef(owner);
  if(!rooted_owner||env->ExceptionCheck()){if(rooted_owner)env->DeleteGlobalRef(rooted_owner);return false;}
  jobject rooted_fields=env->NewGlobalRef(fields.value);
  if(!rooted_fields||env->ExceptionCheck()){if(rooted_fields)env->DeleteGlobalRef(rooted_fields);env->DeleteGlobalRef(rooted_owner);return false;}
  owner_=static_cast<jclass>(rooted_owner);fields_=rooted_fields;ordinals_=std::move(index);
 }
 const auto found=ordinals_.find(id);
 if(found==ordinals_.end())return fail("Declared Field source does not contain requested opaque ID");
 const jsize count=env->GetArrayLength(static_cast<jarray>(fields_));
 if(env->ExceptionCheck())return false;
 if(found->second<0||found->second>=count)return fail("Declared Field source ordinal is outside current array");
 jobject selected=nullptr;
 struct Root{JNIEnv* env;jobject& value;~Root(){if(value)env->DeleteGlobalRef(value);}} root{env,selected};
 Local field{env,env->GetObjectArrayElement(static_cast<jobjectArray>(fields_),found->second)};
 if(!field.value||env->ExceptionCheck())return fail("Declared Field array lost an original mirror");
 const auto actual=env->FromReflectedField(field.value);if(env->ExceptionCheck())return false;
 if(actual!=id)return fail("Declared Field source ordinal changed opaque ID");
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
 if(!selected)return fail("Declared Field source does not contain requested opaque ID");
 *out=selected;selected=nullptr;return true;
}
};
inline bool Create(JNIEnv* env,jvmtiEnv* ti,jclass owner,jfieldID id,bool statik,jobject* out,std::string* error){
 Cache cache;if(!cache.Bind(env,ti,error)){if(out)*out=nullptr;return false;}const bool ok=cache.Create(env,owner,id,statik,out,error);cache.Clear(env);return ok;
}
}
