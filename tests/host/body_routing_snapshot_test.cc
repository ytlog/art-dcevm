#include "platform/common/capability_backend.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
using namespace artdcevm;
namespace {
char* Copy(const char*s){return ::strdup(s);}
constexpr uintptr_t target=1,parent=2,tag=3;
int mutation=0;
jvmtiError Free(jvmtiEnv*,unsigned char*p){std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass c,char**s,char**){*s=Copy(reinterpret_cast<uintptr_t>(c)==target?"Lcom/example/Fixture;":reinterpret_cast<uintptr_t>(c)==parent?(mutation==5?"Lcom/example/Other;":"Lcom/example/HierarchyBase;"):"Lcom/example/HierarchyMore;");return JVMTI_ERROR_NONE;}
jvmtiError ClassFlags(jvmtiEnv*,jclass,jint*out){*out=mutation==4?0x11:1;return JVMTI_ERROR_NONE;}
jclass Super(JNIEnv*,jclass){return reinterpret_cast<jclass>(parent);}
jboolean Exception(JNIEnv*){return JNI_FALSE;}
void Delete(JNIEnv*,jobject){}
jvmtiError Interfaces(jvmtiEnv*,jclass,jint*n,jclass**p){*n=1;*p=static_cast<jclass*>(std::malloc(sizeof(jclass)));**p=reinterpret_cast<jclass>(tag);return JVMTI_ERROR_NONE;}
jvmtiError Fields(jvmtiEnv*,jclass,jint*n,jfieldID**p){*n=4;*p=static_cast<jfieldID*>(std::malloc(4*sizeof(jfieldID)));for(int i=0;i<4;++i)(*p)[i]=reinterpret_cast<jfieldID>(uintptr_t(i+1));return JVMTI_ERROR_NONE;}
jvmtiError FieldName(jvmtiEnv*,jclass,jfieldID id,char**n,char**d,char**){const char*names[]={"left","number","right","value"};int i=reinterpret_cast<uintptr_t>(id)-1;*n=Copy(names[i]);*d=Copy(i==1||i==3?(mutation==2?"J":"I"):"Ljava/lang/Object;");return JVMTI_ERROR_NONE;}
jvmtiError FieldFlags(jvmtiEnv*,jclass,jfieldID id,jint*f){*f=reinterpret_cast<uintptr_t>(id)==2?8:1;return JVMTI_ERROR_NONE;}
jvmtiError Methods(jvmtiEnv*,jclass,jint*n,jmethodID**p){*n=3;*p=static_cast<jmethodID*>(std::malloc(3*sizeof(jmethodID)));for(int i=0;i<3;++i)(*p)[i]=reinterpret_cast<jmethodID>(uintptr_t(i+1));return JVMTI_ERROR_NONE;}
jvmtiError MethodName(jvmtiEnv*,jmethodID id,char**n,char**d,char**){const char*names[]={"gone","keep","staticKeep"};*n=Copy(names[reinterpret_cast<uintptr_t>(id)-1]);*d=Copy(mutation==1?"()J":"()I");return JVMTI_ERROR_NONE;}
jvmtiError MethodFlags(jvmtiEnv*,jmethodID id,jint*f){uintptr_t i=reinterpret_cast<uintptr_t>(id);*f=i==1?0x101:i==2?1:9;if(mutation==3&&i==2)*f|=0x10;return JVMTI_ERROR_NONE;}
jvmtiError Potential(jvmtiEnv*,jvmtiCapabilities*c){*c={};c->can_redefine_classes=1;return JVMTI_ERROR_NONE;}
jvmtiError Add(jvmtiEnv*,const jvmtiCapabilities*){return JVMTI_ERROR_NONE;}
JNIEnv* jni; jvmtiEnv* ti;
jint GetEnv(JavaVM*,void**p,jint v){*p=v==JVMTI_VERSION_1_2?static_cast<void*>(ti):static_cast<void*>(jni);return JNI_OK;}
struct Public:RuntimeBackend{int EnableValidationHooks()override{return 0;}int EnableStandardRedefineRouting()override{return 0;}int InitializeEvolution(JNIEnv*,jobject)override{return 0;}int calls=0,result=0;Operation last=Operation::kEvolve;int Attach(JavaVM*)override{return JNI_OK;}std::string Capabilities()const override{return "{}";}int Apply(Operation op,const std::vector<Definition>&defs)override{assert(defs.size()==1);last=op;++calls;return result;}std::string Analyze(JNIEnv*,const std::vector<Definition>&)override{return "{}";}std::string ErrorName(int)const override{return "error";}};
struct Engine:production_bootstrap::Engine{int recorded=0,applied=0;bool Source13Candidate()const override{return false;}bool Activate(JNIEnv*,std::string*)override{return true;}void RecordDefinitions(JNIEnv*,const std::vector<Definition>&)override{++recorded;}int Apply(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*)override{++applied;return 98;}};
}
int main(int argc,char**argv){assert(argc==2);JNINativeInterface_ jt{};jt.GetSuperclass=Super;jt.ExceptionCheck=Exception;jt.DeleteLocalRef=Delete;JNIEnv j{&jt};jni=&j;
jvmtiInterface_1 tt{};tt.Deallocate=Free;tt.GetClassSignature=Signature;tt.GetClassModifiers=ClassFlags;tt.GetImplementedInterfaces=Interfaces;tt.GetClassFields=Fields;tt.GetFieldName=FieldName;tt.GetFieldModifiers=FieldFlags;tt.GetClassMethods=Methods;tt.GetMethodName=MethodName;tt.GetMethodModifiers=MethodFlags;tt.GetPotentialCapabilities=Potential;tt.AddCapabilities=Add;jvmtiEnv t{&tt};ti=&t;
JNIInvokeInterface_ vt{};vt.GetEnv=GetEnv;JavaVM vm{&vt};auto p=std::make_unique<Public>();auto*pub=p.get();Engine*engine=nullptr;
CapabilityBackend b(std::move(p),{},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});assert(b.Attach(&vm)==JNI_OK);
std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char>dex((std::istreambuf_iterator<char>(in)),{});assert(!dex.empty());std::vector<Definition>defs={{reinterpret_cast<jclass>(target),dex}};
assert(b.Apply(Operation::kEvolve,defs)==0&&pub->last==Operation::kBody&&pub->calls==1); // works before private readiness
assert(b.InitializeCommonCapabilities(jni,reinterpret_cast<jobject>(target))==0);
for(mutation=1;mutation<=5;++mutation){assert(b.Apply(Operation::kEvolve,defs)==98);assert(pub->calls==1&&engine->recorded==0);}
mutation=0;pub->result=41;assert(b.Apply(Operation::kEvolve,defs)==41&&engine->recorded==0);pub->result=0;
assert(b.Apply(Operation::kEvolve,defs)==0&&pub->last==Operation::kBody&&engine->recorded==1&&engine->applied==5);
std::cout<<"Actual live-schema + immutable DEX body routing: pre-private public route, same-count signature/type/flags/class negative, failed-schema recovery, successful-only single RecordDefinitions passed\n";
}
