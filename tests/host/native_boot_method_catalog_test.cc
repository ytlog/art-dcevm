#include "platform/common/native_boot_method_catalog.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
using namespace artdcevm;
namespace {
JNIEnv env;JNINativeInterface_ jni{};jvmtiEnv ti;jvmtiInterface_1 table{};
int loads=0,globals=0,allocations=0,dispatches=0,leaf_calls=0,decode_calls=0,bytecode_calls=0;
bool bad_codec=false,no_capability=false,pending=false;unsigned per_class=1,bad_at=0,pending_at=UINT32_MAX;
void* manager=reinterpret_cast<void*>(0x9898);
jclass Class(unsigned n){return reinterpret_cast<jclass>(uintptr_t(0x100+n));}
jmethodID Id(unsigned n){return reinterpret_cast<jmethodID>(uintptr_t(0x700+n));}
void* Native(unsigned n){return reinterpret_cast<void*>(uintptr_t(0x900+n));}
void* Allocate(size_t n){++allocations;return std::malloc(n);}
char* Text(const char*s){auto*p=static_cast<char*>(Allocate(std::strlen(s)+1));std::strcpy(p,s);return p;}
jobject JNICALL Global(JNIEnv*,jobject o){++globals;return o;}
void JNICALL DeleteGlobal(JNIEnv*,jobject){--globals;}
void JNICALL DeleteLocal(JNIEnv*,jobject){}
jboolean JNICALL Same(JNIEnv*,jobject a,jobject b){return a==b;}
jboolean JNICALL Exception(JNIEnv*){return pending;}
jobject JNICALL Reflect(JNIEnv*,jclass,jmethodID id,jboolean){return reinterpret_cast<jobject>(id);}
jvmtiError JNICALL Caps(jvmtiEnv*,jvmtiCapabilities*c){*c={};c->can_get_bytecodes=!no_capability;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL AddCaps(jvmtiEnv*,const jvmtiCapabilities*){return JVMTI_ERROR_NOT_AVAILABLE;}
jvmtiError JNICALL Loaded(jvmtiEnv*,jint*n,jclass**out){++loads;*n=loads==1?1:2;*out=static_cast<jclass*>(Allocate(*n*sizeof(jclass)));for(int i=0;i<*n;++i)(*out)[i]=Class(i);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Loader(jvmtiEnv*,jclass,jobject*out){*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Status(jvmtiEnv*,jclass,jint*out){*out=JVMTI_CLASS_STATUS_PREPARED;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Signature(jvmtiEnv*,jclass c,char**out,char**){*out=Text(c==Class(0)?"Lboot/One;":"Lboot/Two;");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Methods(jvmtiEnv*,jclass c,jint*n,jmethodID**out){*n=per_class;*out=static_cast<jmethodID*>(Allocate(per_class*sizeof(jmethodID)));for(unsigned i=0;i<per_class;++i)(*out)[i]=Id((c==Class(0)?0:100)+i);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Modifiers(jvmtiEnv*,jmethodID,jint*out){*out=9;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Name(jvmtiEnv*,jmethodID id,char**name,char**out,char**){char unique[32];std::snprintf(unique,sizeof(unique),"method%u",unsigned(uintptr_t(id)-uintptr_t(Id(0))));*name=Text(unique);*out=Text("(II)I");return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Declaring(jvmtiEnv*,jmethodID id,jclass*out){*out=Class(uintptr_t(id)<uintptr_t(Id(100))?0:1);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Bytecodes(jvmtiEnv*,jmethodID id,jint*n,unsigned char**out){++bytecode_calls;*n=4;*out=static_cast<unsigned char*>(Allocate(4));const unsigned char scalar[]={0x12,0,0x0f,0},call[]={0x71,0,0,0};std::memcpy(*out,id==Id(0)?scalar:call,4);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Locals(jvmtiEnv*,jmethodID,jint*out){*out=2;return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Deallocate(jvmtiEnv*,unsigned char*p){--allocations;std::free(p);return JVMTI_ERROR_NONE;}
void* Decode(void*m,jmethodID id){assert(m==manager);++decode_calls;return Native(uintptr_t(id)-uintptr_t(Id(0)));}
void* Leaf(const abi::common::RunnableJniScope&s,jobject reflected){assert(s.self&&s.vm&&s.env==&env);++leaf_calls;const unsigned idx=uintptr_t(reflected)-uintptr_t(Id(0));if(idx==pending_at)pending=true;return bad_codec&&idx==bad_at?Native(999):Native(idx);}
}
namespace artdcevm::runnable_executor {
runnable_jni::Result With(JNIEnv*e,jobject strong,runnable_jni::Operation f,void*p){assert(e==&env&&strong&&globals>0);++dispatches;runnable_jni::Context actual{reinterpret_cast<void*>(0x111),e,reinterpret_cast<void*>(0x222)};return f(actual,p)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}
int main(){
 jni.NewGlobalRef=Global;jni.DeleteGlobalRef=DeleteGlobal;jni.DeleteLocalRef=DeleteLocal;jni.IsSameObject=Same;jni.ExceptionCheck=Exception;jni.ToReflectedMethod=Reflect;env.functions=&jni;
 table.GetCapabilities=Caps;table.AddCapabilities=AddCaps;table.GetLoadedClasses=Loaded;table.GetClassLoader=Loader;table.GetClassStatus=Status;table.GetClassSignature=Signature;table.GetClassMethods=Methods;table.GetMethodModifiers=Modifiers;table.GetMethodName=Name;table.GetMethodDeclaringClass=Declaring;table.GetBytecodes=Bytecodes;table.GetMaxLocals=Locals;table.Deallocate=Deallocate;ti.functions=&table;
 native_boot_method_catalog::Catalog catalog;std::string error;native_boot_method_catalog::Codec codec{manager,Decode,Leaf};
 assert(catalog.Prepare(&env,&ti,codec,&error));assert(loads==3&&globals==2&&allocations==0&&catalog.Complete());
 assert(catalog.Roots().size()==2&&catalog.Methods().size()==2);
 assert(catalog.Find(Native(0))->declaring_signature=="Lboot/One;"&&catalog.Find(Native(0))->name=="method0");
 assert(catalog.Find(Native(0))->body==native_boot_method_catalog::Body::kPrimitiveLocal);
 assert(catalog.Find(Native(100))->body==native_boot_method_catalog::Body::kUnknown);
 assert(!catalog.Find(Native(99)));assert(dispatches==2&&leaf_calls==2&&decode_calls==2&&bytecode_calls==2);catalog.Clear(&env);assert(globals==0);
 // Real producer must enumerate and prove all members, not truncate the boot
 // catalog. Only the Runnable boundary count changes from per-method to per-Class.
 loads=dispatches=leaf_calls=decode_calls=bytecode_calls=0;per_class=64;
 assert(catalog.Prepare(&env,&ti,codec,&error)&&catalog.Methods().size()==128);
 assert(dispatches==2&&leaf_calls==128&&decode_calls==128&&bytecode_calls==128&&globals==2&&allocations==0);
 for(unsigned owner:{0u,100u})for(unsigned n=0;n<64;++n)assert(catalog.Find(Native(owner+n)));
 catalog.Clear(&env);assert(globals==0);
 loads=dispatches=leaf_calls=decode_calls=bytecode_calls=0;bad_codec=true;bad_at=63;
 assert(!catalog.Prepare(&env,&ti,codec,&error)&&!catalog.Complete()&&globals==0&&allocations==0);
 assert(dispatches==1&&leaf_calls==64&&decode_calls==64&&bytecode_calls==64);
 bad_codec=false;bad_at=0;
 loads=dispatches=leaf_calls=decode_calls=bytecode_calls=0;pending_at=31;
 assert(!catalog.Prepare(&env,&ti,codec,&error)&&pending&&!catalog.Complete()&&globals==0&&allocations==0);
 assert(dispatches==1&&leaf_calls==32&&decode_calls==31&&bytecode_calls==64);
 pending=false;pending_at=UINT32_MAX;per_class=1;

 loads=0;bad_codec=true;assert(!catalog.Prepare(&env,&ti,codec,&error));assert(!catalog.Complete()&&globals==0&&allocations==0);bad_codec=false;
 no_capability=true;assert(!catalog.Prepare(&env,&ti,codec,&error));assert(error=="boot-catalog-bytecode-capability-unavailable"&&globals==0&&allocations==0);
 using native_boot_method_catalog::Classify;using native_boot_method_catalog::Body;
 const unsigned char scalar[]={0x12,0,0xf,0};assert(Classify(scalar,4,"()I")==Body::kPrimitiveLocal);
 assert(Classify(scalar,4,"()V")==Body::kUnknown);assert(Classify(scalar,3,"()I")==Body::kUnknown);assert(Classify(scalar,4,"(Ljava/lang/Object;)I")==Body::kUnknown);
 const unsigned char wide_truncated[]={0x18,0,0,0};assert(Classify(wide_truncated,4,"()J")==Body::kUnknown);
 const unsigned char field[]={0x60,0,0,0,0xf,0};assert(Classify(field,6,"()I")==Body::kUnknown);
 const unsigned char payload[]={0,1,0,0};assert(Classify(payload,4,"()I")==Body::kUnknown);
 std::cout<<"boot catalog: public fixedpoint/encoded IDs/independent source leaf/roots/unknown bytecode/capability denial PASS\n";
}
