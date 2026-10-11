#pragma once
#include "native_call_graph_stack_fixture.h"
#include "platform/common/native_call_graph_execution.h"
#include "platform/common/redefinition_gate.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include <map>
namespace execution_fixture {
using namespace artdcevm;
std::vector<unsigned char> dex;std::map<jfieldID,void*>field_aliases;
runnable_jni::Bridge bridge;int proxy_id,name_id,component_id;size_t public_enumerations=0;
void*parent_method=nullptr;
void* MethodAt(size_t i){if(i==3)return parent_method;return reinterpret_cast<char*>(old_method)+32*i;}
size_t MethodIndex(jmethodID id){for(size_t i=0;i<4;++i)if(id==reinterpret_cast<jmethodID>(uintptr_t(MethodAt(i))|1u))return i;std::abort();}
jvmtiError JNICALL Capabilities(jvmtiEnv*,jvmtiCapabilities*out){*out={};out->can_get_bytecodes=1;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Phase(jvmtiEnv*,jvmtiPhase*out){*out=JVMTI_PHASE_ONLOAD;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Loaded(jvmtiEnv*,jint*n,jclass**out){++public_enumerations;*n=2;*out=static_cast<jclass*>(std::malloc(2*sizeof(jclass)));(*out)[0]=reinterpret_cast<jclass>(old_class);(*out)[1]=reinterpret_cast<jclass>(parent_class);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Signature(jvmtiEnv*,jclass c,char**out,char**){*out=strdup(c==reinterpret_cast<jclass>(old_class)?"LFixture;":"Ljava/lang/Object;");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Status(jvmtiEnv*,jclass,jint*out){*out=JVMTI_CLASS_STATUS_VERIFIED|JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_INITIALIZED;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassFlags(jvmtiEnv*,jclass,jint*out){*out=1;return JVMTI_ERROR_NONE;}
jobject replacement_loader=nullptr;
jvmtiError JNICALL Loader(jvmtiEnv*,jclass klass,jobject*out){*out=klass==reinterpret_cast<jclass>(new_class)||klass==reinterpret_cast<jclass>(old_class)?replacement_loader:nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Interfaces(jvmtiEnv*,jclass,jint*n,jclass**out){*n=0;*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassMethods(jvmtiEnv*,jclass c,jint*n,jmethodID**out){*n=c==reinterpret_cast<jclass>(old_class)?3:1;*out=nullptr;if(*n){*out=static_cast<jmethodID*>(std::malloc(size_t(*n)*sizeof(jmethodID)));for(size_t i=0;i<size_t(*n);++i)(*out)[i]=reinterpret_cast<jmethodID>(uintptr_t(MethodAt(c==reinterpret_cast<jclass>(old_class)?i:3))|1u);}return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Name(jvmtiEnv*,jmethodID id,char**name,char**signature,char**){const char*names[]{"gone","keep","staticKeep","keep"};*name=strdup(names[MethodIndex(id)]);*signature=strdup("()I");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Flags(jvmtiEnv*,jmethodID id,jint*out){const jint flags[]{0x101,1,9,1};*out=flags[MethodIndex(id)];return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Bytes(jvmtiEnv*,jmethodID id,jint*n,unsigned char**out){const uint16_t keep[]{0x0071,2,0,0x000a,0x000f},scalar[]{0x0012,0x000f};const auto index=MethodIndex(id);assert(index);*n=index==1?sizeof(keep):sizeof(scalar);*out=static_cast<unsigned char*>(std::malloc(size_t(*n)));std::memcpy(*out,index==1?keep:scalar,size_t(*n));return JVMTI_ERROR_NONE;}
jvmtiError JNICALL ClassFields(jvmtiEnv*,jclass c,jint*n,jfieldID**out){*n=c==reinterpret_cast<jclass>(old_class)?4:0;*out=nullptr;if(*n){*out=static_cast<jfieldID*>(std::malloc(4*sizeof(jfieldID)));for(size_t i=0;i<4;++i)(*out)[i]=reinterpret_cast<jfieldID>(uintptr_t(old_fields[i])|1u);}return JVMTI_ERROR_NONE;}
size_t FieldIndex(jfieldID id){for(size_t i=0;i<4;++i)if(id==reinterpret_cast<jfieldID>(uintptr_t(old_fields[i])|1u))return i;std::abort();}
jvmtiError JNICALL FieldName(jvmtiEnv*,jclass,jfieldID id,char**name,char**signature,char**){size_t i=FieldIndex(id);*name=strdup(::names[i]);*signature=strdup(::types[i]);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL FieldFlags(jvmtiEnv*,jclass,jfieldID id,jint*out){*out=FieldIndex(id)?1:8;return JVMTI_ERROR_NONE;}
jclass JNICALL Parent(JNIEnv*,jclass c){Jni();return c==reinterpret_cast<jclass>(old_class)?reinterpret_cast<jclass>(parent_class):nullptr;}
jboolean JNICALL Assignable(JNIEnv*,jclass from,jclass to){Jni();return from==to||to==reinterpret_cast<jclass>(parent_class);}
jmethodID JNICALL PublicMethod(JNIEnv*,jclass c,const char*n,const char*signature){Jni();if(c==early_owner_fixture::klass)return early_owner_fixture::GetMethod(&env,c,n,signature);if(!std::strcmp(n,"getComponentType"))return reinterpret_cast<jmethodID>(&component_id);if(!std::strcmp(n,"getName"))return reinterpret_cast<jmethodID>(&name_id);return reinterpret_cast<jmethodID>(&proxy_id);}
jclass JNICALL FindClass(JNIEnv*e,const char*n){return !std::strcmp(n,"java/lang/Thread")?early_owner_fixture::FindClass(e,n): ::Find(e,n);}
jobject JNICALL LocalRef(JNIEnv*,jobject p){Jni();return p;}
jobject JNICALL GlobalRef(JNIEnv*e,jobject p){return p==early_owner_fixture::thread?early_owner_fixture::Global(e,p): ::Global(e,p);}
void JNICALL DeleteGlobalRef(JNIEnv*e,jobject p){if(p==early_owner_fixture::thread)early_owner_fixture::DeleteGlobal(e,p);else ::Delete(e,p);}
jboolean JNICALL Proxy(JNIEnv*,jclass,jmethodID,const jvalue*){Jni();return JNI_FALSE;}
jobject JNICALL Component(JNIEnv*,jobject,jmethodID,const jvalue*){Jni();return nullptr;}
jobject JNICALL ReflectedField(JNIEnv*,jclass c,jfieldID id,jboolean is_static){Jni();const size_t i=FieldIndex(id);assert(c==reinterpret_cast<jclass>(old_class)&&bool(is_static)==!i);return reinterpret_cast<jobject>(&mirrors[i]);}
const modern_field_metadata::FieldMetadata* metadata=nullptr;
bool StageField(void*,JNIEnv*e,jclass c,jfieldID id,bool is_static,std::string*){auto mirror=e->ToReflectedField(c,id,is_static);void*field=mirror?FieldLeaf(mirror):nullptr;if(mirror)e->DeleteLocalRef(mirror);if(!field||metadata->FieldDeclaringClass(field)!=c)return false;field_aliases.emplace(id,field);return true;}
void* DecodeField(void*,jclass,jfieldID id){auto i=field_aliases.find(id);return i==field_aliases.end()?nullptr:i->second;}
void* NativeField(void*,void*,jfieldID id){return DecodeField(nullptr,nullptr,id);}
void* DecodeMethod(void*,jmethodID id){return MethodAt(MethodIndex(id));}
void* MethodClass(void*,void*m){return metadata->MethodDeclaringClass(m);}
bool SameMethod(void*,void*a,void*b){
 // Original named comparator leaf protocol; independent public declarations
 // include both same keep()I and different gone/staticKeep signatures.
 const char*names[]{"gone","keep","staticKeep","keep"};size_t ai=4,bi=4;
 for(size_t i=0;i<4;++i){if(a==MethodAt(i))ai=i;if(b==MethodAt(i))bi=i;}
 return ai<4&&bi<4&&!std::strcmp(names[ai],names[bi]);
}
bool DecodeGlobal(void*,jobject p,void**out){*out=p;return p!=nullptr;}
const modern_field_metadata::ReadContext* stopped_read=nullptr;
bool Readable(void*,const void*p,size_t n){size_t extent=0;if(stopped)return stopped_read&&stopped_read->extent(stopped_read->owner,p,&extent)&&extent>=n;return method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
void* OriginalReflect(const abi::common::RunnableJniScope&s,jobject p){return Leaf({s.self,s.env,s.vm},p);}
jmethodID JNICALL Reflect(JNIEnv*e,jobject p){Jni();abi::common::RunnableJniScope scope{thread.data(),e,&::runtime};return reinterpret_cast<jmethodID>(bridge.OnFromReflectedMethod(scope,p));}
jvmtiError Direct(void*,void*,void*,const void*,int32_t,void*){return JVMTI_ERROR_NONE;}
void* Resolve(void*linker_owner,void*self,uint32_t index,void*caller,uint32_t kind){assert(linker_owner==&linker&&self==thread.data()&&caller==MethodAt(1)&&index==2&&kind==0&&!stopped);return MethodAt(2);}
void* RootBarrier(void*p){return reinterpret_cast<void*>(uintptr_t(Read32(p)));}
void* pauser_vtable[3]{};void(*add_proxy)(void*,void*)=nullptr;void(*remove_proxy)(void*,void*)=nullptr;
void Add(void*,void*){assert(!stopped);}void Remove(void*,void*){assert(!stopped);}
struct Hooks final:drain_fixture::Hooks{
 drain_fixture::OwnerHooks owner;
 void*FindSymbol(const char*lib,const char*n)override{
  if(std::strstr(n,"RedefineClassesDirect"))return reinterpret_cast<void*>(Direct);
  if(!std::strcmp(n,"_ZTVN12openjdkjvmti21ClassDefinitionPauserE"))return pauser_vtable;
  if(std::strstr(n,"AddClassLoadCallback"))return reinterpret_cast<void*>(Add);
  if(std::strstr(n,"RemoveClassLoadCallback"))return reinterpret_cast<void*>(execution_fixture::Remove);
  if(!std::strcmp(n,receiver_guard::kCheckedResolveMethod))return reinterpret_cast<void*>(Resolve);
  if(!std::strcmp(n,"artReadBarrierForRootSlow"))return reinterpret_cast<void*>(RootBarrier);
  if(!std::strcmp(n,"_ZN3art16ScopedSuspendAllC2EPKcb")||!std::strcmp(n,"_ZN3art16ScopedSuspendAllD2Ev"))return owner.FindSymbol(lib,n);
  if(std::strstr(n,"Instrumentation10Deoptimize")||std::strstr(n,"Instrumentation12Undeoptimize")||std::strstr(n,"Instrumentation13IsDeoptimized")||std::strstr(n,"Instrumentation11AddListener")||std::strstr(n,"Runtime9instance_E")||std::strstr(n,"AttachCurrentThreadEPKc"))return owner.FindSymbol(lib,n);
  return drain_fixture::Hooks::FindSymbol(lib,n);
 }
 void*Install(void*t,void*p,void**o,std::string*e)override{
  if(t==reinterpret_cast<void*>(Direct)){*o=t;return t;}
  if(t==reinterpret_cast<void*>(Add)){add_proxy=reinterpret_cast<decltype(add_proxy)>(p);*o=t;return t;}
  if(t==reinterpret_cast<void*>(execution_fixture::Remove)){remove_proxy=reinterpret_cast<decltype(remove_proxy)>(p);*o=t;return t;}
  if(t==reinterpret_cast<void*>(early_owner_fixture::Original)||t==reinterpret_cast<void*>(early_owner_fixture::AttachOriginal)||t==reinterpret_cast<void*>(early_owner_fixture::AttachLegacy))return owner.Install(t,p,o,e);
  return drain_fixture::Hooks::Install(t,p,o,e);
 }
};
void Configure(drain_fixture::Environment&environment,jvmtiInterface_1&functions,native_schema_guard_verifier::Bindings&bindings,const char*path){
 dex=File(path);metadata=&environment.metadata;old_class->access_flags=1;static_cast<class_metadata::Class17*>(parent_class)->access_flags=1;
 old_class->iftable=0;static_cast<class_metadata::Class17*>(parent_class)->iftable=0;static_cast<class_metadata::Class17*>(parent_class)->methods=0;
 auto*array=static_cast<unsigned char*>(heap.New(40));Put32(array,0,1);static_cast<class_metadata::Class17*>(parent_class)->methods=uintptr_t(array);parent_method=array+8;
 auto*pm=reinterpret_cast<abi::arm64::android12::Method12*>(parent_method);pm->declaring_class=uint32_t(uintptr_t(parent_class));pm->access_flags=1;pm->dex_method_index=3;
 LinkDex(old_class,dex);static_cast<class_metadata::Class17*>(parent_class)->dex_cache=old_class->dex_cache;old_class->dex_class_def_index=0;old_class->dex_type_index=1;static_cast<class_metadata::Class17*>(parent_class)->dex_class_def_index=1;static_cast<class_metadata::Class17*>(parent_class)->dex_type_index=2;
 const uint32_t field_indices[]{1,3,0,2};for(size_t i=0;i<4;++i)old_fields[i]->dex_field_index=field_indices[i];
 size_t p=Read32(dex.data(),Read32(dex.data(),100)+24);auto leb=[&](){uint32_t n=0;for(unsigned s=0;s<35;s+=7){auto b=dex[p++];n|=uint32_t(b&127)<<s;if(!(b&128))return n;}std::abort();};uint32_t counts[4];for(auto&n:counts)n=leb();for(size_t i=0;i<counts[0]+counts[1];++i){leb();leb();}for(unsigned kind=2;kind<4;++kind){uint32_t index=0;for(size_t i=0;i<counts[kind];++i){index+=leb();leb();const auto offset=leb();if(offset)reinterpret_cast<abi::arm64::android12::Method12*>(MethodAt(index))->data=uintptr_t(dex.data()+offset);}}
 size_t parent_p=Read32(dex.data(),Read32(dex.data(),100)+32+24);p=parent_p;for(unsigned i=0;i<4;++i)leb();leb();leb();pm->data=uintptr_t(dex.data()+leb());
 functions.GetCapabilities=Capabilities;functions.GetPhase=Phase;functions.GetLoadedClasses=Loaded;functions.Deallocate=Deallocate;functions.GetClassSignature=Signature;functions.GetClassStatus=Status;functions.GetClassModifiers=ClassFlags;functions.GetClassLoader=Loader;functions.GetImplementedInterfaces=Interfaces;functions.GetClassMethods=ClassMethods;functions.GetMethodName=Name;functions.GetMethodModifiers=Flags;functions.GetBytecodes=Bytes;functions.GetClassFields=ClassFields;functions.GetFieldName=FieldName;functions.GetFieldModifiers=FieldFlags;
 table.NewLocalRef=LocalRef;table.FindClass=FindClass;table.GetJavaVM=early_owner_fixture::PublicVm;table.CallStaticObjectMethodV=early_owner_fixture::CallStatic;table.NewGlobalRef=GlobalRef;table.DeleteGlobalRef=DeleteGlobalRef;table.GetSuperclass=Parent;table.IsAssignableFrom=Assignable;table.GetMethodID=PublicMethod;table.GetStaticMethodID=PublicMethod;table.CallStaticBooleanMethodA=Proxy;table.CallObjectMethodA=Component;table.ToReflectedField=ReflectedField;table.FromReflectedMethod=Reflect;
 assert(runnable_jni::Bridge::Bind(reinterpret_cast<void*>(OriginalReflect),&bridge));
 bindings.vm=&::runtime;bindings.roots={nullptr,drain_fixture::RootScope,drain_fixture::RootOracle,drain_fixture::RootDecode,drain_fixture::RootElement};bindings.metadata=metadata;bindings.methods=&environment.model;bindings.decode_global=DecodeGlobal;bindings.decode_method=DecodeMethod;bindings.decode_native_field=NativeField;bindings.decode_field=DecodeField;bindings.stage_field=StageField;bindings.method_class=MethodClass;bindings.same_method=SameMethod;bindings.readable=Readable;bindings.writable=Readable;
}
}
