#include "platform/android13/runtime_access.h"
#include "platform/android13/runtime_access_abi.h"
#include "platform/android13/selective_deopt_abi.h"
#include "platform/android13/native_object_access.h"
#include "device_profile_fixture.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace artdcevm;using namespace artdcevm::android13;
namespace {
runtime_access_abi::RuntimePrefix runtime{};void* instance=&runtime;JavaVM vm{};
int mode=0,depth=0,field_calls=0,leaf_calls=0,global_count=0,hidden_lookups=0;bool moved=false,pending=false;
alignas(8) unsigned char native[5][8]{};struct Mirror {size_t index;bool fresh;};Mirror before[5]={{0,false},{1,false},{2,false},{3,false},{4,false}},after[5]={{0,true},{1,true},{2,true},{3,true},{4,true}};
jclass Class(size_t index){return reinterpret_cast<jclass>(uintptr_t(0x2000+index*0x1000));}
jfieldID Id(size_t i){return i%2?reinterpret_cast<jfieldID>(uintptr_t(i*2+1)):reinterpret_cast<jfieldID>(mode==1?uintptr_t(0x8000+i*8):reinterpret_cast<uintptr_t>(native[i]));}
size_t Index(jfieldID id){for(size_t i=0;i<5;++i)if(Id(i)==id)return i;assert(false);return 0;}
void* Method(void*,jmethodID id){return id;}
void* Field(void* manager,jfieldID id){assert(depth==1&&manager==runtime.jni_id_manager);++field_calls;moved=true;if(mode==1&&!(reinterpret_cast<uintptr_t>(id)&1))return id;return (mode==9&&Index(id)%2)||(mode==12&&Index(id)==4)?native[0]:native[Index(id)];}
void* Leaf(void* raw){assert(depth==1);++leaf_calls;auto& mirror=*static_cast<Mirror*>(raw);assert(mirror.fresh);return native[mirror.index];}
struct Hooks:HookEngine {bool Initialize(std::string*)override{return true;}bool Remove(void*,std::string*)override{assert(false);return false;}void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}
 void* FindSymbol(const char*,const char* name)override{if(!std::strcmp(name,runtime_access_abi::kRuntimeInstance))return &instance;if(!std::strcmp(name,runtime_access_abi::kDecodeMethodId))return reinterpret_cast<void*>(Method);if(!std::strcmp(name,runtime_access_abi::kDecodeFieldId))return reinterpret_cast<void*>(Field);if(!std::strcmp(name,"_ZN3art6mirror5Field11GetArtFieldEv"))return mode==2?nullptr:reinterpret_cast<void*>(Leaf);assert(false);return nullptr;}};
void Public(){assert(depth==0);}
jboolean Exception(JNIEnv*){Public();return pending;}
jint VM(JNIEnv*,JavaVM** out){Public();*out=mode==7?reinterpret_cast<JavaVM*>(0x8000):&vm;return JNI_OK;}
jclass ObjectClass(JNIEnv*,jobject object){Public();return reinterpret_cast<jclass>(object);}
jobject Global(JNIEnv*,jobject object){Public();++global_count;return object;}
void DeleteGlobal(JNIEnv*,jobject){Public();--global_count;}
void DeleteLocal(JNIEnv*,jobject){Public();}
jboolean Same(JNIEnv*,jobject a,jobject b){Public();return a==b;}
jclass Super(JNIEnv*,jclass k){Public();return k==Class(2)?nullptr:Class(2);}
jfieldID Lookup(JNIEnv*,jclass k,const char* name,const char*){Public();if(k==Class(2)){++hidden_lookups;assert(false&&"hidden Object fields must not use public name lookup");}size_t i=k==Class(0)?0:2;i+=!std::strcmp(name,"staticValue");return mode==3?Id((i+1)%4):Id(i);}
jobject Reflect(JNIEnv*,jclass k,jfieldID id,jboolean stat){Public();size_t i=Index(id);assert(k==Class(i/2)&&stat==i%2);if(mode==13&&i==4){pending=true;return nullptr;}return reinterpret_cast<jobject>(&before[i]);}
jfieldID Unreflect(JNIEnv*,jobject object){Public();auto* mirror=reinterpret_cast<Mirror*>(object);return mode==5||(mode==11&&mirror->index==4)?Id((mirror->index+1)%4):Id(mirror->index);}
jvmtiError Free(jvmtiEnv*,unsigned char* raw){Public();std::free(raw);return JVMTI_ERROR_NONE;}
jvmtiError Fields(jvmtiEnv*,jclass k,jint* count,jfieldID** ids){Public();if(mode==8)return JVMTI_ERROR_INTERNAL;*count=k==Class(2)?(mode>=10?1:0):2;*ids=*count?static_cast<jfieldID*>(std::malloc(2*sizeof(jfieldID))):nullptr;if(k==Class(2)){if(*count)(*ids)[0]=Id(4);}else {size_t first=k==Class(0)?0:2;(*ids)[0]=Id(first+1);(*ids)[1]=Id(first);}return JVMTI_ERROR_NONE;}
jvmtiError Name(jvmtiEnv*,jclass,jfieldID id,char** name,char** sig,char**){Public();*name=strdup(Index(id)==4?"shadow$_klass_":Index(id)%2?"staticValue":"value");*sig=strdup("I");return JVMTI_ERROR_NONE;}
jvmtiError Flags(jvmtiEnv*,jclass,jfieldID id,jint* flags){Public();*flags=1|(Index(id)%2?8:0);return JVMTI_ERROR_NONE;}
jvmtiError Owner(jvmtiEnv*,jclass,jfieldID id,jclass* owner){Public();*owner=mode==4?Class(2):Class(Index(id)/2);return JVMTI_ERROR_NONE;}
}
namespace artdcevm::android13 {
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(nullptr),supported_(mode!=6),transitioned_(false){if(supported_){++depth;moved=false;}}
NativeObjectAccess::~NativeObjectAccess(){assert(!transitioned_);if(supported_)--depth;}
void* NativeObjectAccess::Decode(jobject ref)const{assert(depth==1);auto* mirror=reinterpret_cast<Mirror*>(ref);return moved?static_cast<void*>(&after[mirror->index]):static_cast<void*>(mirror);}
}
int main(int argc,char** argv){assert(argc==2);mode=std::atoi(argv[1]);Hooks hooks;std::string error;
 runtime.class_linker=reinterpret_cast<void*>(0x1000);runtime.jni_id_manager=reinterpret_cast<void*>(0x2000);runtime.jit=reinterpret_cast<void*>(0x3000);runtime.jit_code_cache=reinterpret_cast<void*>(0x4000);runtime.heap=reinterpret_cast<void*>(0x5000);runtime.java_vm=&vm;runtime.thread_list=reinterpret_cast<void*>(0x6000);
 RuntimeReceivers receivers{runtime.class_linker,runtime.jni_id_manager,&vm,runtime.thread_list,reinterpret_cast<unsigned char*>(&runtime)+offsetof(selective_abi::RuntimePrefix,instrumentation)};OwnedJitReceivers jit{runtime.jit,runtime.jit_code_cache,runtime.heap};assert(ConfigureRuntimeAccess(hooks,MatchedProfileForTest(),&error,&receivers,&jit));
 assert(!DecodeFieldId(Id(0))&&!DecodeFieldId(Id(1))&&field_calls==0);
 JNINativeInterface_ jni{};jni.ExceptionCheck=Exception;jni.GetJavaVM=VM;jni.GetObjectClass=ObjectClass;jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.IsSameObject=Same;jni.GetSuperclass=Super;jni.GetFieldID=Lookup;jni.GetStaticFieldID=Lookup;jni.ToReflectedField=Reflect;jni.FromReflectedField=Unreflect;JNIEnv env{&jni};jvmtiInterface_1_ ti_functions{};ti_functions.Deallocate=Free;ti_functions.GetClassFields=Fields;ti_functions.GetFieldName=Name;ti_functions.GetFieldModifiers=Flags;ti_functions.GetFieldDeclaringClass=Owner;jvmtiEnv ti{&ti_functions};
 bool result=CalibrateRuntimeFieldIds(hooks,&env,&ti,reinterpret_cast<jobject>(Class(0)),reinterpret_cast<jobject>(Class(1)),&error);assert(result==(mode==0||mode==10)&&global_count==0&&depth==0&&hidden_lookups==0&&pending==(mode==13));
 if(result){assert(field_calls==(mode==10?6:4)&&leaf_calls==(mode==10?6:4));NativeObjectAccess access(&env);for(size_t i=0;i<4;++i)assert(DecodeFieldId(Id(i))==native[i]);}
 else {int prior=field_calls;assert(!DecodeFieldId(Id(0))&&!DecodeFieldId(Id(1))&&field_calls==prior);}
 std::cout<<"Legacy actual runtime TU mode "<<mode<<": complete rooted public Field proof, hidden ancestor mirror proof, even DEX-ID rejection, movement reload, no preproof consumers passed\n";
}
