#pragma once
// Shared public/native leaf boundary for the actual typed-heap Service and
// per-Run Proof. No readiness or validation implementation is substituted.
#include "platform/common/native_typed_heap_walk.h"
namespace {
void* schema_typed_class=nullptr;void* schema_typed_instance=nullptr;void* schema_typed_array=nullptr;
std::array<abi::arm64::Field*,2> schema_typed_fields{};
std::array<Mirror,2> schema_typed_mirrors{};
using SchemaTypedCtor=void(*)(void*,void*,void*,size_t*,bool);
using SchemaTypedRef=bool(*)(const void*,void*,void*,uint32_t);
SchemaTypedCtor schema_typed_ctor=nullptr;SchemaTypedRef schema_typed_ref=nullptr;
std::unordered_map<jobject,size_t> schema_typed_global_refs;
auto schema_typed_prior_global=table.NewGlobalRef;auto schema_typed_prior_delete_global=table.DeleteGlobalRef;
jobject SchemaTypedGlobal(JNIEnv*e,jobject object){auto root=schema_typed_prior_global(e,object);if(root)++schema_typed_global_refs[root];return root;}
void SchemaTypedDeleteGlobal(JNIEnv*e,jobject object){auto at=schema_typed_global_refs.find(object);if(at!=schema_typed_global_refs.end()){assert(at->second);if(!--at->second)schema_typed_global_refs.erase(at);}schema_typed_prior_delete_global(e,object);}
size_t SchemaTypedGlobals(){size_t n=0;for(auto entry:schema_typed_global_refs)n+=entry.second;return n;}
size_t schema_typed_local_refs=0;
jobject SchemaTypedLocal(JNIEnv*e,jobject object){++schema_typed_local_refs;return Global(e,object);}
size_t schema_typed_walks=0;int schema_typed_component_id,schema_typed_primitive_id,schema_typed_field_type_id;
void SchemaTypedOriginalCtor(void*,void*self,void*h,size_t*failures,bool refs){assert(stopped&&self==current&&h==&owner_heap&&failures&&refs);}
bool SchemaTypedOriginalRef(const void*,void*,void*,uint32_t){assert(stopped);return true;}
size_t SchemaTypedOriginalHeap(void*h,bool refs){assert(stopped&&h==&owner_heap&&refs);++schema_typed_walks;size_t failures=0;uintptr_t visitor=0;schema_typed_ctor(&visitor,current,h,&failures,true);
 for(auto entry:{std::pair<void*,uint32_t>{schema_typed_array,12},{schema_typed_array,16},{schema_typed_instance,8},{schema_typed_class,128}})schema_typed_ref(&visitor,entry.first,Root(static_cast<char*>(entry.first)+entry.second),entry.second);
 // Original whole-heap verifier includes the independently held Runtime roots.
 for(auto*value:{schema_typed_array,schema_typed_instance,schema_typed_class,ref_a,ref_b})schema_typed_ref(&visitor,nullptr,value,0);
 return failures;
}
bool SchemaTypedAssignable(void*target,void*value){for(auto*p=value;p;p=Root(static_cast<char*>(p)+offsetof(class_metadata::Class17,super_class)))if(p==target)return true;return false;}
auto schema_typed_prior_static_method=table.GetStaticMethodID;
jmethodID SchemaTypedStaticMethod(JNIEnv*e,jclass c,const char*n,const char*s){if(!std::strcmp(n,"identityHashCode"))return reinterpret_cast<jmethodID>(&hash_id);return schema_typed_prior_static_method(e,c,n,s);}
auto schema_typed_prior_method=table.GetMethodID;
auto schema_typed_prior_object=table.CallObjectMethodV;
auto schema_typed_prior_loader=static_cast<decltype(jvmtiInterface_1::GetClassLoader)>(nullptr);
jvmtiError SchemaTypedLoader(jvmtiEnv*e,jclass c,jobject*out){if(schema_typed_prior_loader)return schema_typed_prior_loader(e,c,out);*out=reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(c))+offsetof(class_metadata::Class17,class_loader)));return JVMTI_ERROR_NONE;}
auto schema_typed_prior_fields=static_cast<decltype(jvmtiInterface_1::GetClassFields)>(nullptr);
auto schema_typed_prior_name=static_cast<decltype(jvmtiInterface_1::GetFieldName)>(nullptr);
auto schema_typed_prior_flags=static_cast<decltype(jvmtiInterface_1::GetFieldModifiers)>(nullptr);
auto schema_typed_prior_reflected=table.ToReflectedField;
jmethodID SchemaTypedMethod(JNIEnv*e,jclass c,const char*n,const char*s){
 if(!std::strcmp(n,"getComponentType"))return reinterpret_cast<jmethodID>(&schema_typed_component_id);
 if(!std::strcmp(n,"isPrimitive"))return reinterpret_cast<jmethodID>(&schema_typed_primitive_id);
 if(!std::strcmp(n,"getType"))return reinterpret_cast<jmethodID>(&schema_typed_field_type_id);
 return schema_typed_prior_method(e,c,n,s);
}
jobject SchemaTypedObject(JNIEnv*e,jobject o,jmethodID id,va_list args){
 if(id==reinterpret_cast<jmethodID>(&schema_typed_component_id)){Jni();return reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(o))+offsetof(class_metadata::Class17,component_type)));}
 if(id==reinterpret_cast<jmethodID>(&schema_typed_field_type_id)){Jni();assert(o);return reinterpret_cast<jobject>(parent_class);}
 return schema_typed_prior_object(e,o,id,args);
}
jboolean SchemaTypedPrimitive(JNIEnv*,jobject,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&schema_typed_primitive_id));return false;}
jvmtiError SchemaTypedFields(jvmtiEnv*e,jclass c,jint*n,jfieldID**out){
 if(c!=schema_typed_class&&schema_typed_prior_fields)return schema_typed_prior_fields(e,c,n,out);
 *n=c==schema_typed_class?2:c==reinterpret_cast<jclass>(old_class)?4:0;*out=nullptr;
 if(*n){*out=static_cast<jfieldID*>(std::malloc(size_t(*n)*sizeof(jfieldID)));for(jint i=0;i<*n;++i)(*out)[i]=reinterpret_cast<jfieldID>(c==schema_typed_class?schema_typed_fields[size_t(i)]:old_fields[size_t(i)]);}return JVMTI_ERROR_NONE;
}
jvmtiError SchemaTypedName(jvmtiEnv*e,jclass c,jfieldID f,char**n,char**s,char**generic){
 if(c!=schema_typed_class&&schema_typed_prior_name)return schema_typed_prior_name(e,c,f,n,s,generic);
 *n=strdup(FieldName(f));*s=strdup(FieldType(f));return JVMTI_ERROR_NONE;
}
jvmtiError SchemaTypedFlags(jvmtiEnv*e,jclass c,jfieldID f,jint*out){
 if(c!=schema_typed_class&&schema_typed_prior_flags)return schema_typed_prior_flags(e,c,f,out);
 *out=jint(reinterpret_cast<abi::arm64::Field*>(f)->access_flags);return JVMTI_ERROR_NONE;
}
jobject SchemaTypedReflected(JNIEnv*e,jclass c,jfieldID f,jboolean statik){
 if(c==schema_typed_class){Jni();for(size_t i=0;i<2;++i)if(reinterpret_cast<jfieldID>(schema_typed_fields[i])==f){assert(statik==bool(i));return reinterpret_cast<jobject>(&schema_typed_mirrors[i]);}std::abort();}
 if(schema_typed_prior_reflected)return schema_typed_prior_reflected(e,c,f,statik);
 Jni();for(size_t i=0;i<4;++i)if(reinterpret_cast<jfieldID>(old_fields[i])==f)return reinterpret_cast<jobject>(&mirrors[i]);std::abort();
}
void ConfigureSchemaTypedPublic(JNINativeInterface_&jni,jvmtiInterface_1&ti){
 if(jni.NewGlobalRef!=SchemaTypedGlobal)schema_typed_prior_global=jni.NewGlobalRef;
 if(jni.DeleteGlobalRef!=SchemaTypedDeleteGlobal)schema_typed_prior_delete_global=jni.DeleteGlobalRef;
 jni.NewGlobalRef=SchemaTypedGlobal;jni.DeleteGlobalRef=SchemaTypedDeleteGlobal;
 if(jni.GetStaticMethodID!=SchemaTypedStaticMethod)schema_typed_prior_static_method=jni.GetStaticMethodID;
 jni.GetStaticMethodID=SchemaTypedStaticMethod;
 if(jni.GetMethodID!=SchemaTypedMethod)schema_typed_prior_method=jni.GetMethodID;
 if(jni.CallObjectMethodV!=SchemaTypedObject)schema_typed_prior_object=jni.CallObjectMethodV;
 if(jni.ToReflectedField!=SchemaTypedReflected)schema_typed_prior_reflected=jni.ToReflectedField;
 if(ti.GetClassLoader!=SchemaTypedLoader)schema_typed_prior_loader=ti.GetClassLoader;
 ti.GetClassLoader=SchemaTypedLoader;
 if(ti.GetClassFields!=SchemaTypedFields)schema_typed_prior_fields=ti.GetClassFields;
 if(ti.GetFieldName!=SchemaTypedName)schema_typed_prior_name=ti.GetFieldName;
 if(ti.GetFieldModifiers!=SchemaTypedFlags)schema_typed_prior_flags=ti.GetFieldModifiers;
 jni.NewLocalRef=SchemaTypedLocal;
 jni.GetMethodID=SchemaTypedMethod;jni.CallObjectMethodV=SchemaTypedObject;jni.CallBooleanMethodV=SchemaTypedPrimitive;jni.ToReflectedField=SchemaTypedReflected;
 jni.GetStaticObjectField=[](JNIEnv*e,jclass c,jfieldID f){return GetObject(e,c,f);};
 jni.IsAssignableFrom=[](JNIEnv*,jclass actual,jclass target)->jboolean{Jni();return SchemaTypedAssignable(target,actual);};
 ti.GetClassFields=SchemaTypedFields;ti.GetFieldName=SchemaTypedName;ti.GetFieldModifiers=SchemaTypedFlags;
}
void PrepareSchemaTypedObjects(){
 if(schema_typed_class)return;
 auto*c=static_cast<class_metadata::Class17*>(heap.New(160));InitializeClass(c);c->access_flags=1;c->iftable=uintptr_t(Root(static_cast<char*>(parent_class)+offsetof(class_metadata::Class17,iftable)));schema_typed_class=c;
 auto*raw=static_cast<unsigned char*>(heap.New(36));Put32(raw,0,2);c->fields=uintptr_t(raw);
 for(size_t i=0;i<2;++i){schema_typed_fields[i]=new(raw+4+16*i)abi::arm64::Field{uint32_t(uintptr_t(c)),i?8u:1u,uint32_t(i),i?128u:8u};schema_typed_mirrors[i]={schema_typed_fields[i]};}
 schema_typed_instance=heap.New(16);Put32(schema_typed_instance,0,uint32_t(uintptr_t(c)));Put32(schema_typed_instance,8,uint32_t(uintptr_t(ref_a)));Put32(c,128,uint32_t(uintptr_t(ref_b)));
 // This independent class is in both public loaded inventory and immutable DEX.
 const auto package=subject_descriptor.substr(0,subject_descriptor.rfind('/')+1);
 public_classes.push_back({c,package+"TypedCalibrationProbe;",parent_class,{}});
}
void CalibrateSchemaTypedHeap(OwnerHooks&hooks,native_typed_heap_walk::Service&walk,modern_field_metadata::FieldMetadata&metadata,jvmtiInterface_1&ti,std::string&error){
 ConfigureSchemaTypedPublic(table,ti);OwnerNative(thread.data());
 schema_typed_array=table.NewObjectArray(&env,2,reinterpret_cast<jclass>(parent_class),nullptr);
 table.SetObjectArrayElement(&env,reinterpret_cast<jobjectArray>(schema_typed_array),0,reinterpret_cast<jobject>(ref_a));table.SetObjectArrayElement(&env,reinterpret_cast<jobjectArray>(schema_typed_array),1,reinterpret_cast<jobject>(ref_b));
 exclusive_owned_roots::Service exclusive;assert(exclusive_owned_roots::Service::Bind(hooks,&exclusive,&error));
 assert(walk.Bind(hooks,&owner_heap,Current,&error));
 if(!walk.Calibrate(&env,reinterpret_cast<jobjectArray>(schema_typed_array),reinterpret_cast<jobject>(ref_a),reinterpret_cast<jobject>(ref_b),exclusive,&error)){std::cerr<<"typed heap array: "<<error<<" walks="<<schema_typed_walks<<"\n";std::abort();}
 assert(!walk.Calibrated());
 if(!walk.CalibrateFields(hooks,&env,reinterpret_cast<jobject>(schema_typed_instance),reinterpret_cast<jclass>(schema_typed_class),reinterpret_cast<jobject>(&schema_typed_mirrors[0]),reinterpret_cast<jobject>(&schema_typed_mirrors[1]),reinterpret_cast<jobject>(ref_a),reinterpret_cast<jobject>(ref_b),metadata,exclusive,&error)){std::cerr<<"typed heap fields: "<<error<<"\n";std::abort();}
 assert(walk.Calibrated()&&schema_typed_walks==2);OwnerRunnable(thread.data());
}
}
const char* SchemaTypedFieldName(void*f){if(!f)return nullptr;for(size_t i=0;i<2;++i)if(f==schema_typed_fields[i])return i?"typedStatic":"typedInstance";return nullptr;}
const char* SchemaTypedFieldType(void*f){return SchemaTypedFieldName(f)?"Ljava/lang/Object;":nullptr;}
void* SchemaTypedSymbol(const char*n){
 if(!std::strcmp(n,native_typed_heap_walk::kVerifyHeap))return reinterpret_cast<void*>(SchemaTypedOriginalHeap);
 if(!std::strcmp(n,native_typed_heap_walk::kVisitorCtor))return reinterpret_cast<void*>(SchemaTypedOriginalCtor);
 if(!std::strcmp(n,native_typed_heap_walk::kVerifyReference))return reinterpret_cast<void*>(SchemaTypedOriginalRef);
 if(!std::strcmp(n,"_ZN3art6mirror5Class16IsAssignableFromENS_6ObjPtrIS1_EE"))return reinterpret_cast<void*>(SchemaTypedAssignable);
 return nullptr;
}
bool SchemaTypedInstall(void*t,void*p,void**original){
 if(t==reinterpret_cast<void*>(SchemaTypedOriginalCtor)){*original=t;schema_typed_ctor=reinterpret_cast<SchemaTypedCtor>(p);return true;}
 if(t==reinterpret_cast<void*>(SchemaTypedOriginalRef)){*original=t;schema_typed_ref=reinterpret_cast<SchemaTypedRef>(p);return true;}
 return false;
}
