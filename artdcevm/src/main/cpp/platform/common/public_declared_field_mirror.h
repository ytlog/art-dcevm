#pragma once
#include <jni.h>
#include <string>
namespace artdcevm::public_declared_field_mirror {
// Class.getDeclaredFieldsUnchecked(false) uses the original source's force_resolve=false
// factory. Do not substitute JNI ToReflectedField (force_resolve=true).
inline bool Create(JNIEnv* env,jclass owner,jfieldID id,bool statik,jobject* out,std::string* error){
 if(out)*out=nullptr;
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!owner||!id||!out||env->ExceptionCheck())return fail("Declared Field source request invalid or pending");
 struct Local{JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}};
 Local type{env,env->GetObjectClass(owner)};
 if(!type.value||env->ExceptionCheck())return fail("Declared Field Class API unavailable");
 auto declared=env->GetMethodID(static_cast<jclass>(type.value),"getDeclaredFieldsUnchecked","(Z)[Ljava/lang/reflect/Field;");
 if(!declared||env->ExceptionCheck())return fail("Declared Field original source accessor unavailable");
 Local fields{env,env->CallObjectMethod(owner,declared,JNI_FALSE)};
 if(!fields.value||env->ExceptionCheck())return fail("Declared Field original source enumeration failed");
 const jsize count=env->GetArrayLength(static_cast<jarray>(fields.value));
 if(env->ExceptionCheck())return false;
 jobject selected=nullptr;
 struct Root{JNIEnv* env;jobject& value;~Root(){if(value)env->DeleteGlobalRef(value);}} root{env,selected};
 for(jsize i=0;i<count;++i){
  Local field{env,env->GetObjectArrayElement(static_cast<jobjectArray>(fields.value),i)};
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
}
