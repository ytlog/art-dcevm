#include "platform/common/capability_backend.h"
#include <cassert>
#include <iostream>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace artdcevm;
namespace {
jvmtiEnv* ti=nullptr;std::atomic<int> getenv_calls{0};
jvmtiError Potential(jvmtiEnv*,jvmtiCapabilities* c){*c={};c->can_redefine_classes=1;return JVMTI_ERROR_NONE;}
jvmtiError Add(jvmtiEnv*,const jvmtiCapabilities* c){assert(c->can_redefine_classes);return JVMTI_ERROR_NONE;}
JNIEnv* env=reinterpret_cast<JNIEnv*>(uintptr_t(0x5000));
jint GetEnv(JavaVM*,void** out,jint version){++getenv_calls;*out=version==JVMTI_VERSION_1_2?reinterpret_cast<void*>(ti):reinterpret_cast<void*>(env);return JNI_OK;}
struct Backend:RuntimeBackend {
 int attached=0,applied=0,initialized=0,validation=0;int init_error=0,apply_result=41;
 mutable int reports=0,analyses=0,error_names=0;std::function<void()> apply_callback;
 int Attach(JavaVM*)override{++attached;return JNI_OK;}
 std::string Capabilities()const override{++reports;return "{\"methodBody\":true,\"nativeAdditive\":true}";}
 int Apply(Operation,const std::vector<Definition>&)override{++applied;if(apply_callback)apply_callback();return apply_result;}
 std::string Analyze(JNIEnv*,const std::vector<Definition>&)override{++analyses;return "public analysis";}
 std::string ErrorName(int)const override{++error_names;return "public error";}
 int EnableValidationHooks()override{++validation;return 0;}
 int EnableStandardRedefineRouting()override{return 0;}
 int InitializeEvolution(JNIEnv*,jobject)override{++initialized;return init_error;}
};
struct Engine:production_bootstrap::Engine {
 bool source=false,allow=true,fields=false,stop=false;int activation=0,applied=0,recorded=0,apply_result=42;
 std::function<void()> apply_callback,record_callback;
 bool Source13Candidate()const override{return source;}
 void RecordDefinitions(JNIEnv*,const std::vector<Definition>&)override{++recorded;if(record_callback)record_callback();}
 bool FieldDeletionReady()const override{return fields&&activation&&allow;}
 bool SourceStopCalibrated()const override{return stop;}
 std::string SourceStopDiagnostic()const override{return stop?"":"source \"proof\" pending";}
 bool Activate(JNIEnv*,std::string* error)override{++activation;if(!allow)*error="guard proof rejected";return allow;}
 int Apply(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*)override{++applied;if(apply_callback)apply_callback();return apply_result;}
};
struct Blocker {
 std::mutex mutex;std::condition_variable condition;bool entered=false,released=false;
 void Hold(){std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();condition.wait(lock,[&]{return released;});}
 void Wait(){std::unique_lock<std::mutex> lock(mutex);assert(condition.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
 void Release(){std::lock_guard<std::mutex> lock(mutex);released=true;condition.notify_all();}
};
void RejectEveryEntry(CapabilityBackend& backend,JavaVM* vm,jobject calibration) {
 const int before=getenv_calls.load();
 const auto start=std::chrono::steady_clock::now();
 for(auto operation:{Operation::kBody,Operation::kStructure,Operation::kEvolve})
  assert(backend.Apply(operation,{})==JVMTI_ERROR_NOT_AVAILABLE);
 assert(backend.InitializeCapabilities(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
 assert(backend.InitializeEvolution(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
 assert(backend.Attach(vm)==JNI_ERR);
 assert(backend.EnableValidationHooks()==JVMTI_ERROR_NOT_AVAILABLE);
 assert(backend.EnableStandardRedefineRouting()==JVMTI_ERROR_NOT_AVAILABLE);
 assert(backend.Analyze(env,{}).find("operationBusy")!=std::string::npos);
 assert(backend.Capabilities().find("operationBusy")!=std::string::npos);
 assert(backend.ErrorName(0)=="Runtime backend operation already in progress");
 assert(getenv_calls.load()==before);
 assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
}
void Concurrency(JavaVM* vm,jobject calibration) {
 // Hold each actual operation at its delegate. A second OS thread must reject
 // every public entry without any extra JNI/JVMTI/public/source/producer work.
 for(auto operation:{Operation::kBody,Operation::kStructure,Operation::kEvolve}) {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Engine* engine=nullptr;
  CapabilityBackend backend(std::move(public_backend),{},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});
  assert(backend.Attach(vm)==JNI_OK&&backend.InitializeCapabilities(env,calibration)==0);
  p->apply_result=0;engine->apply_result=0;Blocker hold;
  if(operation==Operation::kEvolve)engine->apply_callback=[&]{hold.Hold();};
  else p->apply_callback=[&]{hold.Hold();};
  int result=-1;std::thread first([&]{result=backend.Apply(operation,{});});hold.Wait();
  const int pub_calls=p->applied,native_calls=engine->applied,records=engine->recorded;
  RejectEveryEntry(backend,vm,calibration);
  assert(p->applied==pub_calls&&engine->applied==native_calls&&engine->recorded==records);
  assert(p->reports==0&&p->analyses==0&&p->error_names==0&&p->attached==1&&engine->activation==1);
  hold.Release();first.join();assert(result==0);
  p->apply_callback={};engine->apply_callback={};
  assert(backend.Apply(operation,{})==0); // Scope released after completion.
  assert(engine->recorded==(operation==Operation::kEvolve?0:2));
 }
 // The same boundary applies when a proven full source backend owns evolution.
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Backend* source=nullptr;
  CapabilityBackend backend(std::move(public_backend),[&]{auto result=std::make_unique<Backend>();source=result.get();return result;},
    [](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();e->source=true;return e;});
  assert(backend.Attach(vm)==JNI_OK&&backend.InitializeEvolution(env,calibration)==0);
  source->apply_result=0;Blocker hold;source->apply_callback=[&]{hold.Hold();};int result=-1;
  std::thread first([&]{result=backend.Apply(Operation::kEvolve,{});});hold.Wait();
  RejectEveryEntry(backend,vm,calibration);
  assert(source->applied==1&&p->applied==0&&source->reports==0&&source->analyses==0&&source->error_names==0&&source->initialized==1&&source->attached==1);
  hold.Release();first.join();assert(result==0);source->apply_callback={};
  assert(backend.Apply(Operation::kEvolve,{})==0&&source->applied==2);
 }
 // Public success does not release exclusion before RecordDefinitions updates
 // native/provenance state. Keep the complete Apply interval protected.
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Engine* engine=nullptr;
  CapabilityBackend backend(std::move(public_backend),{},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});
  assert(backend.Attach(vm)==JNI_OK&&backend.InitializeCapabilities(env,calibration)==0);p->apply_result=0;
  Blocker hold;engine->record_callback=[&]{hold.Hold();};int result=-1;
  std::thread first([&]{result=backend.Apply(Operation::kBody,{});});hold.Wait();
  RejectEveryEntry(backend,vm,calibration);assert(p->applied==1&&engine->recorded==1&&engine->applied==0);
  hold.Release();first.join();assert(result==0);engine->record_callback={};
  assert(backend.Apply(Operation::kStructure,{})==0&&engine->recorded==2);
 }
 // Preparation uses the same scope: no racing attempted_/source/common state,
 // and a callback cannot recursively initialize or mutate before publication.
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Blocker hold;int productions=0;
  CapabilityBackend* owner=nullptr;
  CapabilityBackend backend(std::move(public_backend),{},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){
   ++productions;RejectEveryEntry(*owner,vm,calibration);hold.Hold();return std::make_unique<Engine>();
  });owner=&backend;assert(backend.Attach(vm)==JNI_OK);int result=-1;
  std::thread first([&]{result=backend.InitializeCapabilities(env,calibration);});hold.Wait();
  RejectEveryEntry(backend,vm,calibration);assert(productions==1&&p->applied==0);
  hold.Release();first.join();assert(result==0&&backend.InitializeCapabilities(env,calibration)==0&&productions==1);
 }
 // Same-thread recursive operations are rejected before any delegates, too.
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Engine* engine=nullptr;
  CapabilityBackend backend(std::move(public_backend),{},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});
  assert(backend.Attach(vm)==JNI_OK&&backend.InitializeCapabilities(env,calibration)==0);
  p->apply_result=0;engine->apply_result=0;
  p->apply_callback=[&]{RejectEveryEntry(backend,vm,calibration);};
  assert(backend.Apply(Operation::kBody,{})==0&&p->applied==1&&engine->recorded==1);
  engine->apply_callback=[&]{RejectEveryEntry(backend,vm,calibration);};
  assert(backend.Apply(Operation::kEvolve,{})==0&&engine->applied==1);
  // RAII also releases the gate if an existing delegate throws in host tests.
  p->apply_callback=[] {throw std::runtime_error("independent delegate failure");};
  try {backend.Apply(Operation::kBody,{});assert(false);}catch(const std::runtime_error&){}
  p->apply_callback={};assert(backend.Apply(Operation::kBody,{})==0&&engine->recorded==2);
 }
}
}
int main(){
 jvmtiInterface_1_ jvmti_functions{};jvmti_functions.GetPotentialCapabilities=Potential;jvmti_functions.AddCapabilities=Add;_jvmtiEnv jvmti_environment{&jvmti_functions};ti=&jvmti_environment;
 JNIInvokeInterface_ functions{};functions.GetEnv=GetEnv;JavaVM vm{&functions};auto calibration=reinterpret_cast<jobject>(uintptr_t(0x6000));
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();int source_created=0;
  CapabilityBackend backend(std::move(public_backend),[&] {++source_created;return std::make_unique<Backend>();},[](JNIEnv*,jvmtiEnv*,jobject,std::string* e)->std::unique_ptr<production_bootstrap::Engine>{*e="missing/wrong independent proof";return nullptr;});
  assert(backend.Attach(&vm)==JNI_OK&&p->attached==1);
  assert(backend.InitializeCapabilities(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
  assert(source_created==0&&backend.EnableValidationHooks()==JVMTI_ERROR_NOT_AVAILABLE);
  assert(backend.Apply(Operation::kBody,{})==41&&backend.Apply(Operation::kStructure,{})==41);
  assert(backend.Apply(Operation::kEvolve,{})==JVMTI_ERROR_NOT_AVAILABLE&&p->applied==2);
  assert(backend.Capabilities().find("\"partialEvolutionAvailable\":false")!=std::string::npos);
 }
 // A proven common source family preserves the public engine and cannot grant
 // the fixed13 route, even when full initialization was requested first.
 {auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Engine* engine=nullptr;int source_created=0;
  CapabilityBackend backend(std::move(public_backend),[&] {++source_created;return std::make_unique<Backend>();},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});
  assert(backend.Attach(&vm)==JNI_OK);
  assert(backend.InitializeEvolution(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
  assert(backend.InitializeCapabilities(env,calibration)==0&&engine->activation==1&&source_created==0);
  assert(backend.Apply(Operation::kEvolve,{})==42&&engine->applied==1);
  assert(backend.Apply(Operation::kBody,{})==41&&p->applied==1);
  assert(backend.InitializeEvolution(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
  assert(backend.Capabilities().find("\"fullDcevmComplete\":false")!=std::string::npos);
  assert(backend.Capabilities().find("\"fieldDeletion\":false")!=std::string::npos);
  engine->fields=true;
  assert(backend.Capabilities().find("\"fieldDeletion\":true")!=std::string::npos);
  assert(backend.Capabilities().find("\"nativeDelete\":true")!=std::string::npos);
  assert(backend.Capabilities().find("\"evolutionAvailable\":false")!=std::string::npos);
  assert(backend.Capabilities().find("\"sourceStopCalibrated\":false")!=std::string::npos);
  assert(backend.Capabilities().find("source \\\"proof\\\" pending")!=std::string::npos);
  engine->stop=true;
  assert(backend.Capabilities().find("\"sourceStopCalibrated\":true")!=std::string::npos);
  assert(backend.Capabilities().find("\"evolutionAvailable\":false")!=std::string::npos);
  assert(backend.Capabilities().find("\"fullDcevmComplete\":false")!=std::string::npos);
 }
 {
  Engine* engine=nullptr;Backend* source=nullptr;
  CapabilityBackend backend(std::make_unique<Backend>(),[&] {auto s=std::make_unique<Backend>();source=s.get();return s;},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();e->source=true;engine=e.get();return e;});
  assert(backend.Attach(&vm)==JNI_OK&&backend.InitializeEvolution(env,calibration)==0);
  assert(source&&source->attached==1&&source->initialized==1&&engine->activation==0);
  assert(backend.Apply(Operation::kEvolve,{})==41&&source->applied==1);
 }
 {
  Engine* engine=nullptr;
  CapabilityBackend backend(std::make_unique<Backend>(),[]{return std::make_unique<Backend>();},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();e->allow=false;engine=e.get();return e;});
  assert(backend.Attach(&vm)==JNI_OK&&backend.InitializeCapabilities(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
  assert(engine->activation==1&&backend.Apply(Operation::kEvolve,{})==JVMTI_ERROR_NOT_AVAILABLE);
  assert(backend.Apply(Operation::kBody,{})==41);
 }
 {
  int created=0;Engine* engine=nullptr;
  CapabilityBackend backend(std::make_unique<Backend>(),[&] {++created;auto b=std::make_unique<Backend>();b->init_error=JVMTI_ERROR_NOT_AVAILABLE;return b;},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();e->source=true;engine=e.get();return e;});
  assert(backend.Attach(&vm)==JNI_OK);
  assert(backend.InitializeCapabilities(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE);
  assert(backend.InitializeCapabilities(env,calibration)==JVMTI_ERROR_NOT_AVAILABLE&&created==1&&engine->activation==0);
  assert(backend.Apply(Operation::kBody,{})==41&&backend.Apply(Operation::kEvolve,{})==JVMTI_ERROR_NOT_AVAILABLE);
 }
 {
  auto public_backend=std::make_unique<Backend>();auto* p=public_backend.get();Engine* engine=nullptr;
  CapabilityBackend backend(std::move(public_backend),[]{return std::make_unique<Backend>();},[&](JNIEnv*,jvmtiEnv*,jobject,std::string*){auto e=std::make_unique<Engine>();engine=e.get();return e;});
  assert(backend.Attach(&vm)==JNI_OK&&backend.InitializeCapabilities(env,calibration)==0);
  // Failed public evolution cannot publish bytes as successful provenance.
  assert(backend.Apply(Operation::kStructure,{})==41&&engine->recorded==0);
  p->apply_result=0;
  assert(backend.Apply(Operation::kBody,{})==0&&engine->recorded==1);
  assert(backend.Apply(Operation::kStructure,{})==0&&engine->recorded==2);
  assert(backend.Apply(Operation::kStructure,{})==0&&engine->recorded==3);
 }
 Concurrency(&vm,calibration);
 std::cout<<"Capability backend: proof refusal, public preservation, common/source separation and full/subset initialization, true cross-thread all-entry refusal, recursive refusal and provenance interval exclusion passed\n";
}
