// Actual Direct dispatcher, final native-vector reader and weak reservation.
// The VM/JNI/ART leaves are explicit host doubles; no private receipt is forged.
#include "platform/common/redefinition_gate.cc"
#include "platform/common/production_final_dex.h"
#include "platform/abi/arm64/common/transformed_definition.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace artdcevm;
namespace {
JNIEnv*current_env=nullptr;int self_object,runtime_object,env_object,class_object;
bool exception=false,weak_oom=false,throw_original=false,mutate=false;
size_t originals=0,created=0,deleted=0,cleared=0;
jvmtiError result=JVMTI_ERROR_NONE;
struct Weak {jobject klass;};std::vector<Weak*>weak_refs;
jobject Resolve(jobject value){for(auto*weak:weak_refs)if(value==reinterpret_cast<jobject>(weak))return weak->klass;return value;}
jboolean Exception(JNIEnv*){return exception;}
void Clear(JNIEnv*){exception=false;++cleared;}
jweak NewWeak(JNIEnv*,jobject value){if(weak_oom){exception=true;return nullptr;}auto*w=new Weak{value};weak_refs.push_back(w);++created;return reinterpret_cast<jweak>(w);}
void DeleteWeak(JNIEnv*,jweak value){auto*w=reinterpret_cast<Weak*>(value);auto it=std::find(weak_refs.begin(),weak_refs.end(),w);assert(it!=weak_refs.end());weak_refs.erase(it);delete w;++deleted;}
jobject NewLocal(JNIEnv*,jobject value){return Resolve(value);}
void DeleteLocal(JNIEnv*,jobject){}
jboolean Same(JNIEnv*,jobject a,jobject b){return Resolve(a)==Resolve(b);}
jint GetEnv(JavaVM*,void**out,jint version){assert(version==JNI_VERSION_1_6);*out=current_env;return JNI_OK;}
jvmtiError Phase(jvmtiEnv*,jvmtiPhase*out){*out=JVMTI_PHASE_ONLOAD;return JVMTI_ERROR_NONE;}
void*Current(){return &self_object;}
unsigned char bytes[]{1,2,3};
jvmtiError Body(void*env,void*runtime,void*self,const void*,int32_t,void*){
 assert(env==&env_object&&runtime==&runtime_object&&self==&self_object);++originals;
 if(mutate)bytes[0]=9;
 if(throw_original)throw std::runtime_error("original unwind");
 return result;
}
void Target(){std::abort();}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void*FindSymbol(const char*,const char*symbol)override{
  if(!std::strcmp(symbol,abi::common::kCurrentThread))return reinterpret_cast<void*>(Current);
  assert(std::strstr(symbol,"RedefineClassesDirect"));return reinterpret_cast<void*>(Target);
 }
 void*Install(void*,void*,void**out,std::string*)override{*out=reinterpret_cast<void*>(Body);return this;}
 bool Remove(void*,std::string*)override{std::abort();}
};
}
int main(int argc,char**argv){
 assert(argc==2);const std::string mode=argv[1];
 JNINativeInterface_ functions{};functions.ExceptionCheck=Exception;functions.ExceptionClear=Clear;
 functions.NewWeakGlobalRef=NewWeak;functions.DeleteWeakGlobalRef=DeleteWeak;
 functions.NewLocalRef=NewLocal;functions.DeleteLocalRef=DeleteLocal;functions.IsSameObject=Same;
 JNIEnv env{&functions};current_env=&env;JNIInvokeInterface_ vm_functions{};vm_functions.GetEnv=GetEnv;JavaVM vm{&vm_functions};
 jvmtiInterface_1 ti_functions{};ti_functions.GetPhase=Phase;jvmtiEnv ti{&ti_functions};Hooks hooks;std::string error;
 assert(redefinition_gate::InstallStartup(hooks,&vm,&ti,&error));
 using Native=abi::arm64::android_common::TransformedDefinition;
 Native definition{};definition.klass=reinterpret_cast<jclass>(&class_object);definition.initialized=1;
 definition.dex_begin=bytes;definition.dex_size=sizeof(bytes);
 abi::arm64::android_common::TransformedDefinitions source{&definition,&definition+1,&definition+1};
 const auto invoke=[&]{return redefinition_gate::Proxy(&env_object,&runtime_object,&self_object,&source,0,nullptr);};
 production_bootstrap::Provenance records;production_bootstrap::FinalDex recorder(records);
 if(mode=="history"){
  assert(invoke()==JVMTI_ERROR_NONE);
  assert(!recorder.Install(hooks,&vm,&error)&&!records.Trusted());
 }else{
  assert(recorder.Install(hooks,&vm,&error));
  if(mode=="success"){
   mutate=true;assert(invoke()==JVMTI_ERROR_NONE);mutate=false;
   std::vector<unsigned char>installed;assert(records.Find(&env,definition.klass,&installed)&&installed[0]==1&&bytes[0]==9);
   result=JVMTI_ERROR_FAILS_VERIFICATION;assert(invoke()==result);
   assert(records.Find(&env,definition.klass,&installed)&&installed[0]==1&&records.Trusted());
   // A preexisting exception is never cleared by observational bookkeeping.
   exception=true;assert(invoke()==result&&exception&&!cleared&&records.Trusted());exception=false;
   result=JVMTI_ERROR_NONE;const auto weak_count=created;
   {redefinition_gate::Lease lease(&vm);assert(lease.Acquired()&&invoke()==JVMTI_ERROR_NONE);}
   assert(created==weak_count&&records.Trusted());
   definition.initialized=0;result=JVMTI_ERROR_NONE;assert(invoke()==result&&!records.Trusted());
  }else if(mode=="oom"){
   weak_oom=true;assert(invoke()==JVMTI_ERROR_NONE&&!exception&&cleared==1&&!records.Trusted());
  }else{
   assert(mode=="unwind");throw_original=true;
   try{invoke();assert(false);}catch(const std::runtime_error&){}
   assert(!records.Trusted()&&!redefinition_gate::direct);
  }
 }
 records.Clear(&env);assert(weak_refs.empty()&&created==deleted&&originals);
 std::cout<<"Actual final DEX observer composition "<<mode<<" protocol PASS; no private execution permission granted\n";
}
