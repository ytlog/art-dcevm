#pragma once
#include "platform/common/source_cache_public_roles.h"
// Public Java/JVMTI leaf boundary doubles for the independently generated
// immutable original namespace. Public.Prepare/Observe/Rooted are production.
namespace {
struct PreparedCacheRoles {
 source_cache_public_roles::Public public_roles;std::vector<unsigned char> bytes;
 source_cache_role_catalog::Catalog catalog;void* primitive=nullptr;std::map<jobject,std::string> strings;
 JNIEnv* env=nullptr;JNINativeInterface_ prior{};jvmtiInterface_1 prior_ti{};
 static inline PreparedCacheRoles* active=nullptr;int for_name=0,type_id=0;
 ~PreparedCacheRoles(){if(env)public_roles.Clear(env);assert(strings.empty());}
 static jclass Class(const std::string& descriptor){auto&s=*active;if(descriptor=="I")return reinterpret_cast<jclass>(s.primitive);for(auto&p:public_classes)if(p.descriptor==descriptor)return reinterpret_cast<jclass>(p.native);return nullptr;}
 static jclass FindClass(JNIEnv*e,const char*n){if(!std::strcmp(n,"java/lang/Integer"))return reinterpret_cast<jclass>(active->primitive);return active->prior.FindClass(e,n);}
 static jstring String(JNIEnv*,const char*n){Jni();auto*p=new int;auto root=reinterpret_cast<jobject>(p);active->strings.emplace(root,n);return reinterpret_cast<jstring>(root);}
 static void DeleteLocal(JNIEnv*e,jobject object){auto at=active->strings.find(object);if(at!=active->strings.end()){delete reinterpret_cast<int*>(object);active->strings.erase(at);}else active->prior.DeleteLocalRef(e,object);}
 static jmethodID StaticMethod(JNIEnv*e,jclass c,const char*n,const char*d){if(!std::strcmp(n,"forName"))return reinterpret_cast<jmethodID>(&active->for_name);for(size_t i=0;i<3;++i)if(c==reinterpret_cast<jclass>(old_class)&&public_method_names[i]==std::string(n)&&!std::strcmp(d,"()I")&&(public_method_flags[i]&8))return reinterpret_cast<jmethodID>(reinterpret_cast<char*>(old_method)+32*i);return active->prior.GetStaticMethodID(e,c,n,d);}
 static jmethodID Method(JNIEnv*e,jclass c,const char*n,const char*d){for(size_t i=0;i<3;++i)if(c==reinterpret_cast<jclass>(old_class)&&public_method_names[i]==std::string(n)&&!std::strcmp(d,"()I")&&!(public_method_flags[i]&8))return reinterpret_cast<jmethodID>(reinterpret_cast<char*>(old_method)+32*i);return active->prior.GetMethodID(e,c,n,d);}
 static jobject StaticCall(JNIEnv*,jclass,jmethodID id,va_list args){Jni();assert(id==reinterpret_cast<jmethodID>(&active->for_name));auto text=va_arg(args,jstring);assert(va_arg(args,int)==JNI_FALSE);auto loader=va_arg(args,jobject);assert(loader==reinterpret_cast<jobject>(uintptr_t(old_class->class_loader)));auto name=active->strings.at(text);std::replace(name.begin(),name.end(),'.','/');if(name.front()!='[')name="L"+name+";";return Class(name);}
 static jfieldID StaticField(JNIEnv*,jclass c,const char*n,const char*d){Jni();if(c==reinterpret_cast<jclass>(active->primitive)&&!std::strcmp(n,"TYPE")&&!std::strcmp(d,"Ljava/lang/Class;"))return reinterpret_cast<jfieldID>(&active->type_id);for(size_t i=0;i<4;++i)if(c==reinterpret_cast<jclass>(old_class)&&std::string(n)==names[i]&&!std::strcmp(d,types[i])&&i==0)return reinterpret_cast<jfieldID>(old_fields[i]);return nullptr;}
 static jfieldID Field(JNIEnv*,jclass c,const char*n,const char*d){Jni();for(size_t i=1;i<4;++i)if(c==reinterpret_cast<jclass>(old_class)&&std::string(n)==names[i]&&!std::strcmp(d,types[i]))return reinterpret_cast<jfieldID>(old_fields[i]);return nullptr;}
 static jobject StaticObject(JNIEnv*e,jclass c,jfieldID id){if(id==reinterpret_cast<jfieldID>(&active->type_id))return reinterpret_cast<jobject>(active->primitive);return active->prior.GetStaticObjectField(e,c,id);}
 static jobject ReflectedField(JNIEnv*,jclass c,jfieldID id,jboolean statik){Jni();const auto i=PublicFieldIndex(id);assert(c==reinterpret_cast<jclass>(old_class)&&bool(statik)==(i==0));return reinterpret_cast<jobject>(&mirrors[i]);}
 static jobject ReflectedMethod(JNIEnv*,jclass c,jmethodID id,jboolean statik){Jni();const auto i=PublicMethodIndex(id);assert(c==reinterpret_cast<jclass>(old_class)&&bool(statik)==bool(public_method_flags[i]&8));return reinterpret_cast<jobject>(executable[i]);}
 static jvmtiError MethodOwner(jvmtiEnv*,jmethodID id,jclass*out){(void)PublicMethodIndex(id);*out=reinterpret_cast<jclass>(old_class);return JVMTI_ERROR_NONE;}
 static jvmtiError FieldOwner(jvmtiEnv*,jclass,jfieldID id,jclass*out){(void)PublicFieldIndex(id);*out=reinterpret_cast<jclass>(old_class);return JVMTI_ERROR_NONE;}
 static jvmtiError Loader(jvmtiEnv*,jclass c,jobject*out){*out=reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(c))+offsetof(class_metadata::Class11,class_loader)));return JVMTI_ERROR_NONE;}
 static jvmtiError Signature(jvmtiEnv*,jclass c,char**out,char**){if(c==reinterpret_cast<jclass>(active->primitive))*out=strdup("I");else *out=strdup(Public(c).descriptor.c_str());return JVMTI_ERROR_NONE;}
 static void* DecodeMethod(void*,jmethodID id){(void)PublicMethodIndex(id);return id;}
 static void* DecodeField(void*,jfieldID id){(void)PublicFieldIndex(id);return id;}
 bool Prepare(const std::vector<unsigned char>& original,JNIEnv*e,jvmtiEnv*ti,const modern_field_metadata::FieldMetadata& metadata,std::string*error){
  env=e;bytes=original;if(!source_cache_role_catalog::Read(bytes.data(),bytes.size(),&catalog,error))return false;
  auto*file=static_cast<dex_mapping_abi::Prefix*>(original_file);*file={nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
  old_class->dex_class_def_index=0;old_class->dex_type_index=int32_t(Read32(bytes.data(),Read32(bytes.data(),100)));
  for(size_t i=0;i<catalog.fields.size();++i){auto&r=catalog.fields[i];size_t n=0;while(n<4&&r.name!=names[n])++n;assert(n<4&&r.descriptor==types[n]&&(r.access&0xffffu)==(n==0?8u:1u));old_fields[n]->dex_field_index=uint32_t(i);}
  primitive=heap.New(160);schema_cache_primitive=primitive;Put32(primitive,0,uint32_t(uintptr_t(meta_class)));
  prior=*e->functions;prior_ti=*ti->functions;active=this;
  auto&jni=*const_cast<JNINativeInterface_*>(e->functions);auto&v=*const_cast<jvmtiInterface_1*>(ti->functions);
  jni.FindClass=FindClass;jni.NewStringUTF=String;jni.DeleteLocalRef=DeleteLocal;jni.GetStaticMethodID=StaticMethod;jni.GetMethodID=Method;jni.CallStaticObjectMethodV=StaticCall;jni.GetStaticFieldID=StaticField;jni.GetFieldID=Field;jni.GetStaticObjectField=StaticObject;jni.ToReflectedField=ReflectedField;jni.ToReflectedMethod=ReflectedMethod;
  v.GetClassSignature=Signature;v.GetClassLoader=Loader;v.GetClassFields=GuardFields;v.GetFieldName=GuardFieldName;v.GetFieldModifiers=GuardFieldFlags;v.GetClassMethods=GuardMethods;v.GetMethodName=GuardMethodName;v.GetMethodModifiers=GuardMethodFlags;v.GetFieldDeclaringClass=FieldOwner;v.GetMethodDeclaringClass=MethodOwner;v.GetImplementedInterfaces=GuardInterfaces;
  source_cache_public_roles::Bindings binding{this,DecodeMethod,DecodeField,Leaf,FieldLeaf,&metadata,method_metadata::kAndroid12Onward};
  // This is the pre-Run ordinary native producer boundary; its internal
  // executor callback returns to Native before original source resumes.
  OwnerNative(thread.data());
  const bool accepted=public_roles.Prepare(e,ti,catalog,bytes,reinterpret_cast<jclass>(old_class),reinterpret_cast<jobject>(published_cache),binding,error);
  OwnerRunnable(thread.data());
  jni=prior;v=prior_ti;active=nullptr;assert(strings.empty());return accepted;
 }
 void Witness(native_schema_run_state::CacheCalibration* w){
  assert(public_roles.Ready());w->public_roles=&public_roles;w->counts={uint32_t(catalog.types.size()),uint32_t(catalog.fields.size()),uint32_t(catalog.methods.size())};w->type_index=uint32_t(old_class->dex_type_index);w->field_index=0;w->method_index=0;w->native_field=public_roles.Fields()[0].native;w->native_method=public_roles.Methods()[0].native;
  auto*c=static_cast<type_cache::Dual*>(published_cache);auto*pairs=reinterpret_cast<type_cache::Pair*>(c->types);for(size_t i=0;i<1024;++i)pairs[i]={0,i==0?1u:0u};pairs[w->type_index%1024]={uint32_t(uintptr_t(old_class)),w->type_index};
  auto*types=static_cast<uint32_t*>(heap.New(catalog.types.size()*4));types[w->type_index]=uint32_t(uintptr_t(old_class));c->types_array=uintptr_t(types);
  auto*fields=reinterpret_cast<method_cache::Pair*>(c->fields);fields[0]={uintptr_t(w->native_field),0};auto*methods=reinterpret_cast<method_cache::Pair*>(c->methods);methods[0]={uintptr_t(w->native_method),0};
  auto*fa=static_cast<std::atomic<uintptr_t>*>(heap.New(catalog.fields.size()*8));fa[0]=uintptr_t(w->native_field);c->fields_array=uintptr_t(fa);auto*ma=static_cast<std::atomic<uintptr_t>*>(heap.New(catalog.methods.size()*8));ma[0]=uintptr_t(w->native_method);c->methods_array=uintptr_t(ma);
 }
};
}
