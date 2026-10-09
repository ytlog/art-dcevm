#include "platform/common/production_bootstrap.h"
#include "platform/common/bootstrap_dex_oracle.h"
#include "platform/common/production_provenance.h"
#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/runtime_service_calibration.h"
#include "platform/common/owned_jit_receivers.h"
#include "platform/common/owned_method_table.h"
#include "platform/common/native_method_table.h"
#include "platform/common/native_member_provider.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/modern_method_guards.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/modern_source_commit.h"
#include "platform/common/native_owned_removal.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/prepared_root_registry.h"
#include "platform/abi/arm64/android-common/jni_global_ref.h"
#include "platform/common/redefinition_gate.h"
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/native_removal_policy.h"
#include "platform/android13/schema_snapshot.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstdio>
#include "platform/common/native_structural_reflection.h"
#include "platform/common/runnable_executor.h"
#include "platform/android13/selective_deopt_abi.h"
#include "hooks/shadow_hook_engine.h"
#include <cstring>
#include <map>
#include <algorithm>
namespace artdcevm::production_bootstrap {
namespace {
bool Fail(std::string* e,const char* text){if(e)*e=text;return false;}
void JNICALL Marker(JNIEnv*,jobject) {}
struct Roots {
 JNIEnv* env;std::vector<jobject> values;
 ~Roots(){for(auto p:values)if(p)env->DeleteGlobalRef(p);}
 jobject Own(jobject p){auto r=p?env->NewGlobalRef(p):nullptr;if(r)values.push_back(r);return r;}
};
struct Text {jvmtiEnv* ti;char* p=nullptr;~Text(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}};
struct NativeEngine final:Engine {
 ShadowHookEngine hooks;JavaVM* vm=nullptr;std::vector<jobject> roots;
 ~NativeEngine(){JNIEnv* env=nullptr;if(vm&&vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK){provenance.Clear(env);ClearFieldProofs(env);for(auto& p:bootstrap_field_proofs)contextual_field_id::Release(env,&p);contextual_field_id::Prune(env,retired_fields);for(auto p:roots)env->DeleteGlobalRef(p);}}
 runtime_services::Snapshot runtime;
 method_identity::Calibration model;
 modern_field_metadata::FieldMetadata metadata;
 field_mirror::Calibration field_mirror;
 modern_field_storage::Service storage;
 std::unique_ptr<modern_methods::Binding> methods;
 std::unique_ptr<executable_mirror::Layout> mirrors;
 retirement::RetiredMethodRegistry retired_methods;
 retirement::RetiredFieldRegistry retired_fields;
 std::vector<std::shared_ptr<contextual_field_id::Proof>> target_field_proofs,bootstrap_field_proofs;
 struct StagedClass {jclass owner;size_t field_base;std::vector<jfieldID> ids;std::vector<jobject> mirrors;};
 std::vector<StagedClass> staged_classes;std::vector<jclass> graph_bases;
 abi::arm64::android11::CurrentThread graph_current=nullptr;
 field_mirror::NamedField graph_field_leaf=nullptr;
 void ClearStagedClasses(JNIEnv* env){graph_bases.clear();for(auto& c:staged_classes){for(auto mirror:c.mirrors)env->DeleteGlobalRef(mirror);env->DeleteGlobalRef(c.owner);}staged_classes.clear();}
 void ClearFieldProofs(JNIEnv* env){ClearSchemaRoots(env);ClearStagedClasses(env);for(auto& proof:target_field_proofs)contextual_field_id::Release(env,&proof);target_field_proofs.clear();contextual_field_id::Prune(env,retired_fields);}
 native_cache::Service cache;
 native_structural_reflection::Service reflection;
 native_member_provider::Service provider;
 native_schema_guard_verifier::Verifier schema_verifier;
 native_abstract_instances::Service abstract_instances;
 std::vector<native_schema_policy::Plan> schema_policies;
 bool schema_operation=false,schema_collected=false,graph_decoder_proven=false;
 native_owned_removal::RootedGraph schema_roots;
 JNIEnv* schema_root_env=nullptr;
 abi::arm64::android_common::AddGlobalRef graph_add_global=nullptr;
 static jobject RootManaged(void* raw,void* object){auto& e=*static_cast<NativeEngine*>(raw);if(!object||!e.graph_add_global||!redefinition_gate::Owned(e.vm))return nullptr;return e.graph_add_global(e.runtime.receivers.java_vm,e.graph_current(),{uintptr_t(object)});}
 static void ReleaseManaged(void* raw,jobject root){auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_root_env)std::abort();e.schema_root_env->DeleteGlobalRef(root);}
 prepared_roots::Registry schema_registry{{this,RootManaged,DecodeGraph,ReleaseManaged}};
 source_commit::Plan prepared_schema_plan;
 prepared_roots::Bindings schema_plan_roots;
 std::unique_ptr<source_commit::Services> prepared_schema_service;
 bool schema_finalizing=false;
 using DecodeObject=abi::arm64::android11::ReleaseObjectPointer (*)(const void*,jobject);
 DecodeObject graph_decode=nullptr;
 void* (*graph_decode_method)(void*,jmethodID)=nullptr;
 void* (*graph_decode_field)(void*,jfieldID)=nullptr;
 bool (*graph_same)(void*,void*)=nullptr;
 struct MapRange {uintptr_t begin,end;bool write;};std::vector<MapRange> graph_maps;
 bool RefreshMaps(std::string* error){
  graph_maps.clear();FILE* file=std::fopen("/proc/self/maps","r");if(!file)return Fail(error,"Native caller mapping snapshot unavailable");
  char line[1024];while(std::fgets(line,sizeof(line),file)){unsigned long long a=0,b=0;char mode[5]{};
   if(std::sscanf(line,"%llx-%llx %4s",&a,&b,mode)==3&&mode[0]=='r'&&b>a)graph_maps.push_back({uintptr_t(a),uintptr_t(b),mode[1]=='w'});
  }std::fclose(file);return !graph_maps.empty()||Fail(error,"Empty native caller mapping snapshot");
 }
 static bool GraphMapped(void* raw,const void* address,size_t length,bool write){
  const auto& e=*static_cast<NativeEngine*>(raw);uintptr_t p=uintptr_t(address);
#if defined(__ANDROID__) && defined(__aarch64__)
  p&=UINT64_C(0x00ffffffffffffff);
#endif
  if(!p||length>UINTPTR_MAX-p)return false;
  const auto it=std::upper_bound(e.graph_maps.begin(),e.graph_maps.end(),p,[](uintptr_t value,const auto& range){return value<range.begin;});
  if(it==e.graph_maps.begin())return false;const auto& range=*std::prev(it);return p>=range.begin&&p+length<=range.end&&(!write||range.write);
 }
 static bool GraphReadable(void* raw,const void* address,size_t bytes){
  const auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_finalizing)return GraphMapped(raw,address,bytes,false);
  size_t extent=0;return method_identity::ReadableMappingExtent(address,&extent)&&extent>=bytes;
 }
 static bool GraphWritable(void* raw,const void* address,size_t bytes){return GraphMapped(raw,address,bytes,true);}
 static bool ObserveGraphReaders(const runnable_jni::Context& scope,void* raw){
  auto& e=*static_cast<NativeEngine*>(raw);void* expected=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(scope,e.anchor,&expected)||e.graph_decode(scope.self,e.anchor).reference!=uintptr_t(expected))return false;
  size_t fields=0;
  if(e.method_handles.empty()||e.graph_decode_method(e.runtime.receivers.jni_id_manager,e.method_handles[0].method)!=e.proof_method)return false;
  for(const auto& oracle:e.field_method_handles){if(!oracle.identity||e.graph_decode_field(e.runtime.receivers.jni_id_manager,oracle.field)!=contextual_field_id::NativeField(oracle.identity))return false;++fields;}
  for(const auto& stage:e.staged_classes){void* owner=nullptr;if(!runnable_executor::DecodeStrongGlobal(scope,stage.owner,&owner)||e.graph_decode(scope.self,stage.owner).reference!=uintptr_t(owner))return false;
   for(size_t i=0;i<stage.ids.size();++i){void* native=e.graph_decode_field(e.runtime.receivers.jni_id_manager,stage.ids[i]);if(native!=contextual_field_id::NativeField(e.target_field_proofs[stage.field_base+i]))return false;++fields;}
  }
  e.graph_decoder_proven=fields>0;return e.graph_decoder_proven;
 }
 static bool DecodeGraph(void* raw,jobject global,void** value){
  auto& e=*static_cast<NativeEngine*>(raw);if(!global||!value||!e.graph_decoder_proven||!e.graph_decode||!e.graph_current||!redefinition_gate::Owned(e.vm))return false;
  *value=reinterpret_cast<void*>(e.graph_decode(e.graph_current(),global).reference);return *value!=nullptr;
 }
 static void* DecodeGraphMethod(void* raw,jmethodID id){auto& e=*static_cast<NativeEngine*>(raw);return e.graph_decode_method(e.runtime.receivers.jni_id_manager,id);}
 static void* DecodeGraphField(void* raw,jclass,jfieldID id){auto& e=*static_cast<NativeEngine*>(raw);return e.graph_decode_field(e.runtime.receivers.jni_id_manager,id);}
 static void* GraphMethodClass(void* raw,void* method){return static_cast<NativeEngine*>(raw)->metadata.FieldDeclaringClass(method);}
 static bool GraphSame(void* raw,void* a,void* b){return static_cast<NativeEngine*>(raw)->graph_same(a,b);}
 void ClearSchemaRoots(JNIEnv* env){
  schema_root_env=env;prepared_schema_service.reset();schema_plan_roots={};prepared_schema_plan={};schema_registry.Clear();schema_finalizing=false;
  for(const auto& part:schema_roots.partitions)for(auto array:{part.old_classes,part.new_classes,part.old_instances,part.new_instances})if(array)env->DeleteGlobalRef(array);
  if(schema_roots.mirror)env->DeleteGlobalRef(schema_roots.mirror);schema_roots={};schema_collected=false;schema_policies.clear();schema_verifier.Clear(env);graph_maps.clear();
 }
 bool CaptureSchemaRoots(JNIEnv* env,const native_owned_removal::RootedGraph& graph,native_owned_removal::GraphWindow window,std::string* error){
  if(!graph.mirror||graph.partitions.empty()||!graph.holder)return Fail(error,"Schema window lacks actual rooted Holder partitions");
  if(window==native_owned_removal::GraphWindow::Verified){
   if(schema_roots.mirror)return Fail(error,"Repeated schema Holder roots");schema_roots.holder=graph.holder;schema_roots.mirror=static_cast<jclass>(env->NewGlobalRef(graph.mirror));
   for(const auto& part:graph.partitions){auto copy=part;copy.old_classes=static_cast<jobjectArray>(env->NewGlobalRef(part.old_classes));copy.new_classes=static_cast<jobjectArray>(env->NewGlobalRef(part.new_classes));copy.old_instances=copy.new_instances=nullptr;copy.instance_count=0;schema_roots.partitions.push_back(copy);if(!copy.old_classes||!copy.new_classes)return Fail(error,"Schema Holder global rooting failed");}
  }else{
   if(schema_roots.holder!=graph.holder||!env->IsSameObject(schema_roots.mirror,graph.mirror)||schema_roots.partitions.size()!=graph.partitions.size())return Fail(error,"Schema Holder identity changed between source windows");
   for(size_t i=0;i<graph.partitions.size();++i){auto& before=schema_roots.partitions[i];const auto& now=graph.partitions[i];
    if(before.holder_index!=now.holder_index||before.class_count!=now.class_count||!env->IsSameObject(before.old_classes,now.old_classes)||!env->IsSameObject(before.new_classes,now.new_classes))return Fail(error,"Schema Holder partition/origin changed");
    before.old_instances=static_cast<jobjectArray>(env->NewGlobalRef(now.old_instances));before.new_instances=static_cast<jobjectArray>(env->NewGlobalRef(now.new_instances));before.instance_count=now.instance_count;
    if(now.old_instances&&(!before.old_instances||!before.new_instances))return Fail(error,"Schema instance rooting failed");
   }schema_collected=true;
  }schema_root_env=env;return schema_roots.mirror&&!env->ExceptionCheck();
 }
 static void* GraphReference(void* raw,void* array,size_t offset){return static_cast<NativeEngine*>(raw)->metadata.Reference(array,offset);}
 bool RootedSchemaPlan(source_commit::Plan* plan,std::string* error){
  return rooted_schema_plan::Read(schema_roots,schema_collected,{this,DecodeGraph,GraphReadable,GraphReference},plan,error);
 }
 bool PrepareSchemaPolicy(const source_commit::Plan& plan,std::string* error){
  schema_policies.clear();for(size_t i=0;i<plan.old_classes.size();++i){evolution::ClassSchema before,after;native_schema_policy::Plan policy;
   if(!native_schema_policy::ReadNativeDeclarations(metadata,model,plan.old_classes[i],&before,error)||!native_schema_policy::ReadNativeDeclarations(metadata,model,plan.new_classes[i],&after,error)||!native_schema_policy::Plan::Prepare(before,after,&policy,error))return false;
   schema_policies.push_back(std::move(policy));
  }return true;
 }
 Provenance provenance;
 retirement::Membership allocator_membership;
 jobject anchor=nullptr,input=nullptr;bool activated=false,handles_proven=false;
 std::vector<retirement::MethodHandleOracle> method_handles;
 std::vector<retirement::FieldVarHandleOracle> field_handles;
 std::vector<retirement::FieldMethodHandleOracle> field_method_handles;
 jint original_field_values[2]={};
 void* proof_method=nullptr;
 bool Source13Candidate()const override {
  if(!methods||!mirrors||model.model!=method_identity::Model::kMethod12Onward||
      metadata.StorageModel()!=modern_field_metadata::Model::kSplit)return false;
  size_t extent=0;runtime_services::View view;
  if(!method_identity::ReadableMappingExtent(runtime.runtime,&extent)||
      !runtime_services::Read(runtime_services::Declaration::kAndroid13,runtime.runtime,extent,&view))return false;
  const auto& r=runtime.receivers;
  return view.class_linker==r.class_linker&&view.jni_id_manager==r.jni_id_manager&&
      view.java_vm==r.java_vm&&view.thread_list==r.thread_list&&view.jit==r.jit&&
      view.jit_code_cache==r.jit_code_cache&&view.heap==r.heap&&
      runtime.instrumentation==static_cast<unsigned char*>(runtime.runtime)+offsetof(android13::selective_abi::RuntimePrefix,instrumentation);
 }
 bool Activate(JNIEnv* env,std::string* error)override {
  if(activated)return true;
  struct BootstrapProofs {NativeEngine& engine;JNIEnv* env;~BootstrapProofs(){for(auto& p:engine.bootstrap_field_proofs)contextual_field_id::Release(env,&p);engine.bootstrap_field_proofs.clear();contextual_field_id::Prune(env,engine.retired_fields);}} proof_scope{*this,env};
  if(!retirement::InstallRetirementCleanup(hooks,allocator_membership,retired_fields,retired_methods,error)||
     !retirement::InstallMethodEntryGuards(hooks,env,anchor,runtime.receivers.jni_id_manager,retired_methods,error)||
     !retirement::InstallFieldEntryGuards(hooks,env,anchor,runtime.receivers.jni_id_manager,field_mirror,retired_fields,error)||
     !reflection.Bind(hooks,runtime.runtime,*mirrors,error)||!cache.Bind(hooks,runtime.receivers.thread_list,error)||
     !native_owned_removal::Install(hooks,error))return false;
  if(!retirement::InstallMethodHandleFieldGuards(hooks,error)||
     !retirement::PrepareMethodHandleGuards(hooks,env,runtime.receivers.jni_id_manager,retired_methods,method_handles,error)||
     !retirement::PrepareMethodHandleFieldGuards(hooks,env,runtime.receivers.jni_id_manager,retired_fields,field_method_handles,error)||
     !retirement::InstallFieldVarHandleGuards(hooks,env,field_handles,retired_fields,error))return false;
  jclass java=env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap");
  const char* inputs="(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;)";
  std::string int_sig=std::string(inputs)+"I",bool_sig=std::string(inputs)+"Z";
  auto method=java?env->GetStaticMethodID(java,"invokeMethod",int_sig.c_str()):nullptr;
  auto field=java?env->GetStaticMethodID(java,"invokeField","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)I"):nullptr;
  auto fields=java?env->GetStaticMethodID(java,"exerciseFieldHandles",bool_sig.c_str()):nullptr;
  auto retired=java?env->GetStaticMethodID(java,"verifyRetiredHandles",bool_sig.c_str()):nullptr;
  if(!method||!field||!fields||!retired||env->ExceptionCheck())return Fail(error,"Production public handle calls unavailable");
  if(!retirement::BeginMethodHandleTargetProof(env,method_handles[0].strong_global,method_handles[0].method,error))return false;
  jint original=env->CallStaticIntMethod(java,method,input);
  if(!retirement::EndMethodHandleTargetProof(!env->ExceptionCheck()&&original==38,error))return false;
  for(size_t i=0;i<field_method_handles.size();i++){
   const auto& oracle=field_method_handles[i];
   if(!retirement::BeginMethodHandleFieldTargetProof(env,oracle.strong_global,oracle.field,error))return false;
   jint value=env->CallStaticIntMethod(java,field,input,jint(i));
   if(!retirement::EndMethodHandleFieldTargetProof(!env->ExceptionCheck()&&value==original_field_values[i],error))return false;
  }
  if(env->CallStaticBooleanMethod(java,fields,input)!=JNI_TRUE||env->ExceptionCheck())return Fail(error,"Original field handle behavior differs");
  if(!HandlesReady(this)) {
    if(error)*error="Production handle dispatch targets are not independently proven: methodMH="+
        std::to_string(MethodHandlesReady(this))+",fieldMH="+std::to_string(FieldMethodHandlesReady(this))+
        ",fieldVH="+std::to_string(FieldVarHandlesReady(this))+"; "+
        retirement::DescribeFieldVarHandleGuards(retired_fields);
    return false;
  }
  retired_methods.Retire(proof_method);
  for(const auto& proof:bootstrap_field_proofs){retired_fields.Retire(contextual_field_id::NativeField(proof));contextual_field_id::PublishRetired(proof,retired_fields);}
  if(env->CallStaticBooleanMethod(java,retired,input)!=JNI_TRUE||env->ExceptionCheck())return Fail(error,"Production retired handle exceptions differ");
  env->DeleteLocalRef(java);handles_proven=true;activated=true;return true;
 }
 static bool Ready(void* p){auto& s=*static_cast<NativeEngine*>(p);return s.activated&&s.handles_proven&&HandlesReady(p)&&s.cache.PreparedForCurrentThread();}
 static bool MethodHandlesReady(void* p){return retirement::MethodHandleGuardsReady(static_cast<NativeEngine*>(p)->retired_methods);}
 static bool FieldVarHandlesReady(void* p){return retirement::FieldVarHandleGuardsReady(static_cast<NativeEngine*>(p)->retired_fields);}
 static bool FieldMethodHandlesReady(void* p){return retirement::MethodHandleFieldGuardsReady(static_cast<NativeEngine*>(p)->retired_fields);}
 static bool FieldHandlesReady(void* p){return FieldMethodHandlesReady(p)&&FieldVarHandlesReady(p);}
 static bool HandlesReady(void* p){return MethodHandlesReady(p)&&FieldHandlesReady(p);}
 bool FieldDeletionReady()const override{return activated&&handles_proven;}
 static bool FieldRetired(void* p,void* f){return static_cast<NativeEngine*>(p)->retired_fields.TokenFor(f)!=0;}
 static void RetireField(void* p,void* f){
  auto& engine=*static_cast<NativeEngine*>(p);
  // The original final BeginCommit owns this callback. Every alias was staged
  // in ordinary JNI before extension entry; publication performs no JNI work.
  auto proof=std::find_if(engine.target_field_proofs.begin(),engine.target_field_proofs.end(),[&](const auto& candidate){return contextual_field_id::NativeField(candidate)==f;});
  if(proof==engine.target_field_proofs.end())std::abort();
  engine.retired_fields.Retire(f);contextual_field_id::PublishRetired(*proof,engine.retired_fields);
 }
 static bool Retired(void* p){return current&&current->retired_methods.Contains(p);}
 static bool MethodRetired(void* p,void* m){return static_cast<NativeEngine*>(p)->retired_methods.Contains(m);}
 static void RetireMethod(void* p,void* m){static_cast<NativeEngine*>(p)->retired_methods.Retire(m);}
 static std::unique_ptr<source_commit::Services> PrepareCommit(const source_commit::Plan& p,void* raw,std::string* error){
  auto& s=*static_cast<NativeEngine*>(raw);
  native_member_provider::Dependencies d;d.hooks=&s.hooks;d.methods=s.methods.get();d.method_model=&s.model;
  d.metadata=&s.metadata;d.fields=&s.storage;d.cache=&s.cache;d.runtime=s.runtime;d.reflection=&s.reflection;
  d.retirement={&s,MethodRetired,FieldRetired,RetireMethod,RetireField};d.is_retired=Retired;
  d.allow_field_removal=s.handles_proven;d.handle_guards={&s,MethodHandlesReady,FieldVarHandlesReady,FieldMethodHandlesReady};
  if(s.schema_operation){
   if(&p!=&s.prepared_schema_plan||!s.schema_collected||!s.schema_verifier.Seal(p,error)||!s.PrepareSchemaPolicy(p,error))return nullptr;
   d.expected_removal=native_member_provider::Removal::kSchemaEvolution;d.schemas=&s.schema_policies;d.schema_guards=&s.schema_verifier;
  }
  return s.provider.Prepare(p,d,error);
 }
 static std::unique_ptr<source_commit::Services> FinalizeSchema(const void* holder,const source_commit::Plan** plan,void* raw)noexcept{
  auto& e=*static_cast<NativeEngine*>(raw);if(!plan||holder!=e.schema_roots.holder||!e.prepared_schema_service||!redefinition_gate::Owned(e.vm))return nullptr;
  e.schema_finalizing=true;
  if(!e.schema_registry.Resolve())return nullptr;const auto resolver=e.schema_registry.Final();
  if(!e.schema_plan_roots.CanRefresh(resolver)||!e.schema_verifier.CanRefreshManagedRoots(resolver))return nullptr;
  e.schema_plan_roots.Refresh(resolver);
  if(!rooted_schema_plan::Matches(e.schema_roots,{&e,DecodeGraph,GraphReadable,GraphReference},e.prepared_schema_plan)||
     !e.schema_verifier.RefreshManagedRoots(e.prepared_schema_plan,resolver)||
     !e.provider.RefreshManagedRoots(e.prepared_schema_plan,*e.prepared_schema_service,resolver))return nullptr;
  *plan=&e.prepared_schema_plan;return std::move(e.prepared_schema_service);
 }
 void RecordDefinitions(JNIEnv* env,const std::vector<Definition>& definitions)override{
  for(const auto& definition:definitions)if(!provenance.Store(env,definition.klass,definition.dex))break;
 }
 bool Preflight(JNIEnv*,jvmtiEnv*,jclass,std::string*);
 bool StageGraph(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*);
 static bool CheckGraph(JNIEnv*,jvmtiEnv*,const std::vector<jclass>&,const native_owned_removal::RootedGraph&,native_owned_removal::GraphWindow,void*,std::string*);
 static bool StagedInputs(JNIEnv* env,jvmtiEnv*,const std::vector<Definition>& definitions,void* raw,std::string* error){
  auto& s=*static_cast<NativeEngine*>(raw);
  for(const auto& definition:definitions){size_t found=0;for(const auto& stage:s.staged_classes)if(env->IsSameObject(stage.owner,definition.klass))++found;
   if(found!=1||env->ExceptionCheck())return Fail(error,"Input Class missing from staged graph");}
  return true;
 }
 bool AtomicBatchReady()const override{return activated&&redefinition_gate::Ready(vm)&&native_owned_removal::GraphWindowReady();}
 int Apply(JNIEnv* env,jvmtiEnv* ti,const std::vector<Definition>& defs,std::string* error)override {
  if(!activated||current||defs.empty())return JVMTI_ERROR_NOT_AVAILABLE;
  schema_operation=false;for(const auto& definition:defs){evolution::ClassSchema before,after;std::string narrow_error;
   if(android13::ReadLiveSchema(ti,env,definition.klass,&before,error)!=JVMTI_ERROR_NONE||!evolution::ReadDexDeclarations(definition.dex,&after,error))return JVMTI_ERROR_INVALID_CLASS_FORMAT;
   if(!native_owned_removal::ValidateRemoval(before,after,handles_proven,&narrow_error,false)||(before.methods.size()==after.methods.size()&&before.fields.size()==after.fields.size()))schema_operation=true;
  }
  const bool batch=defs.size()>1||schema_operation;
  std::unique_ptr<redefinition_gate::Lease> lease;
  if(batch){
   if(!AtomicBatchReady()){if(error)*error=redefinition_gate::Ready(vm)?"Named rejectable post-verification graph window unavailable":redefinition_gate::Diagnostic();return JVMTI_ERROR_NOT_AVAILABLE;}
   lease=std::make_unique<redefinition_gate::Lease>(vm);
   if(!lease->Acquired())return Fail(error,"Another normal JVMTI transaction is active"),JVMTI_ERROR_NOT_AVAILABLE;
  }
  ClearFieldProofs(env);
  if(schema_operation&&!abstract_instances.Ready()&&!abstract_instances.Calibrate(hooks,env,metadata,model,error))return JVMTI_ERROR_NOT_AVAILABLE;
  struct FieldProofs {NativeEngine& engine;JNIEnv* env;~FieldProofs(){engine.ClearFieldProofs(env);}} field_scope{*this,env};
  if(batch?!StageGraph(env,ti,defs,error):!Preflight(env,ti,defs[0].klass,error))return JVMTI_ERROR_NOT_AVAILABLE;
  if(!cache.Prepare(error))return JVMTI_ERROR_NOT_AVAILABLE;
  current=this;struct Clear {~Clear(){NativeEngine::current=nullptr;}} clear;
  int result=native_owned_removal::RunBatch(hooks,env,ti,defs,
    {Ready,PrepareCommit,&metadata,this,handles_proven,batch?StagedInputs:nullptr,batch?CheckGraph:nullptr,schema_operation,schema_operation?FinalizeSchema:nullptr},error);
  if(result==JVMTI_ERROR_NONE)RecordDefinitions(env,defs);
  return result;
 }
 static thread_local NativeEngine* current;
};
thread_local NativeEngine* NativeEngine::current=nullptr;
struct Request {
 NativeEngine* engine;jobject owner;jobject storage_class;
 const std::vector<method_identity::Oracle>* method_oracles;
 const std::vector<modern_field_metadata::FieldOracle>* field_oracles;
 evolution::MappedDexMemberCounts counts;
 abi::arm64::android_common::FromReflectedMethod leaf;
 field_mirror::NamedField field_leaf;
 std::string error;
};
bool Observe(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<Request*>(raw);auto& e=*r.engine;
 if(!method_identity::Calibrate(scope,r.leaf,r.method_oracles->data(),r.method_oracles->size(),&e.model,&r.error))return false;
 void* klass=nullptr;if(!runnable_executor::DecodeStrongGlobal(scope,r.owner,&klass))return false;
 method_array::View table;
 if(!owned_method_table::Calibrate(scope,r.leaf,e.model,klass,r.counts,r.method_oracles->data(),r.method_oracles->size(),&table,&r.error))return false;
 e.mirrors=executable_mirror::Layout::Probe(scope,r.leaf,runnable_executor::DecodeStrongGlobal,r.method_oracles->data(),r.method_oracles->size(),&r.error);
 for(const auto& oracle:*r.method_oracles)if(oracle.java_modifiers&0x100u)
  e.methods=modern_methods::Service::BindOwned(e.hooks,scope,r.leaf,oracle,reinterpret_cast<const void*>(Marker),e.runtime.receivers.class_linker,e.model.model,&r.error);
 if(!e.methods||!e.mirrors)return false;
 if(!e.metadata.Calibrate(scope,r.storage_class,"Lcom/github/ytlog/artdcevm/internal/CalibrationFixture;",r.field_oracles->data(),r.field_oracles->size(),&r.error))return false;
 return field_mirror::Calibrate(scope,e.metadata,r.field_leaf,r.storage_class,r.field_oracles->data(),r.field_oracles->size(),&e.field_mirror,&r.error);
}
struct HandleRequest {
 NativeEngine* engine;jobject method;jobject mirrors[2];
 abi::arm64::android_common::FromReflectedMethod method_leaf;
 field_mirror::NamedField field_leaf;
};
bool ObserveHandles(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<HandleRequest*>(raw);auto& e=*r.engine;
 const abi::arm64::android_common::RunnableJniScope native{scope.self,scope.env,scope.vm};
 e.proof_method=r.method_leaf(native,r.method);
 if(!e.proof_method)return false;
 for(size_t i=0;i<2;i++){
  void* mirror=nullptr;if(!runnable_executor::DecodeStrongGlobal(scope,r.mirrors[i],&mirror))return false;
  void* field=r.field_leaf(mirror);
  if(!field||field!=contextual_field_id::NativeField(e.bootstrap_field_proofs[i]))return false;
  e.field_handles[i].observed_field=field;
  e.field_handles[i].reflected_field=r.mirrors[i];
 }
 return e.field_handles[0].observed_field!=e.field_handles[1].observed_field;
}
bool OriginalDeclarations(JNIEnv* env,jobject input,evolution::ClassSchema* schema,evolution::MappedDexMemberCounts* counts,std::string* error){
 jclass type=env->GetObjectClass(input);jfieldID field=type?env->GetFieldID(type,"dex","[[B"):nullptr;
 auto array=field?static_cast<jobjectArray>(env->GetObjectField(input,field)):nullptr;
 if(type)env->DeleteLocalRef(type);if(!array||env->ExceptionCheck())return Fail(error,"Original installed APK DEX unavailable");
 std::vector<std::vector<unsigned char>> files;
 for(jsize n=0;n<env->GetArrayLength(array);++n){
  auto value=static_cast<jbyteArray>(env->GetObjectArrayElement(array,n));
  std::vector<unsigned char> bytes(value?static_cast<size_t>(env->GetArrayLength(value)):0);
  if(value){env->GetByteArrayRegion(value,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));env->DeleteLocalRef(value);}
  if(env->ExceptionCheck()){env->DeleteLocalRef(array);return false;}
  files.push_back(std::move(bytes));
 }
 env->DeleteLocalRef(array);return SelectInstalledClass(files,"Lcom/github/ytlog/artdcevm/internal/EvolutionCalibration;",schema,counts,error);
}
struct AllocatorRequest {
 NativeEngine* engine;const std::vector<method_identity::Oracle>* methods;
 const std::vector<modern_field_metadata::FieldOracle>* fields;
 jobject boot_method;abi::arm64::android_common::FromReflectedMethod method_leaf;
 field_mirror::NamedField field_leaf;std::vector<void*> owned,foreign;std::string error;
};
bool CollectAllocator(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<AllocatorRequest*>(raw);
 const abi::arm64::android_common::RunnableJniScope native{scope.self,scope.env,scope.vm};
 for(const auto& oracle:*r.methods){void* method=r.method_leaf(native,oracle.strong_global_method);if(!method)return false;r.owned.push_back(method);}
 for(const auto& oracle:*r.fields){void* mirror=nullptr;if(!runnable_executor::DecodeStrongGlobal(scope,oracle.strong_global_field,&mirror))return false;void* field=r.field_leaf(mirror);if(!field)return false;r.owned.push_back(field);}
 void* foreign=r.method_leaf(native,r.boot_method);if(!foreign)return false;r.foreign.push_back(foreign);return true;
}
bool ProveAllocator(const exclusive_owned_roots::Context& scope,void* raw){
 auto& r=*static_cast<AllocatorRequest*>(raw);return retirement::CalibrateAllocatorMembership(r.engine->hooks,r.engine->runtime.receivers.class_linker,scope,r.owned,r.foreign,&r.engine->allocator_membership,&r.error);
}
struct TargetRequest {
 NativeEngine* engine;jobject owner;const evolution::ClassSchema* schema;
 std::vector<method_identity::Oracle> methods;std::vector<jmethodID> ids;
 std::vector<modern_field_metadata::FieldOracle> fields;
 abi::arm64::android_common::FromReflectedMethod method_leaf;field_mirror::NamedField field_leaf;
 void* (*decode_method)(void*,jmethodID);std::string error;size_t field_proof_base=0;
};
bool TargetObserve(const runnable_jni::Context& scope,void* raw){
 auto& r=*static_cast<TargetRequest*>(raw);auto& e=*r.engine;void* klass=nullptr;
 if(!runnable_executor::DecodeStrongGlobal(scope,r.owner,&klass))return false;
 // Reuse the independently established declaration; all target identities and
 // table bounds below are checked against this Class's independent DEX bytes.
 evolution::MappedDexMemberCounts counts{};
 for(const auto& m:r.schema->methods){if((m.access&(8u|2u))||m.name=="<init>"||m.name=="<clinit>")++counts.direct_methods;else ++counts.virtual_methods;}
 method_array::View table;
 if(!owned_method_table::ReadProvenDeclaration(scope,r.method_leaf,e.model.model,klass,counts,r.methods.data(),r.methods.size(),&table,&r.error))return false;
 const abi::arm64::android_common::RunnableJniScope native{scope.self,scope.env,scope.vm};
 for(size_t i=0;i<r.methods.size();i++)if(r.method_leaf(native,r.methods[i].strong_global_method)!=r.decode_method(e.runtime.receivers.jni_id_manager,r.ids[i]))return false;
 if(r.fields.empty())return true;
 auto target_metadata=e.metadata;
 if(!target_metadata.Calibrate(scope,r.owner,r.schema->descriptor.c_str(),r.fields.data(),r.fields.size(),&r.error)||target_metadata.StorageModel()!=e.metadata.StorageModel())return false;
 for(size_t i=0;i<r.fields.size();i++){
  void* reflected=nullptr;if(!runnable_executor::DecodeStrongGlobal(scope,r.fields[i].strong_global_field,&reflected))return false;
  void* field=r.field_leaf(reflected);
  if(!field||field!=contextual_field_id::NativeField(e.target_field_proofs[r.field_proof_base+i])||e.metadata.FieldDeclaringClass(field)!=klass)return false;
 }
 return true;
}
bool NativeEngine::Preflight(JNIEnv* env,jvmtiEnv* ti,jclass target,std::string* error){
 Text signature{ti};if(ti->GetClassSignature(target,&signature.p,nullptr)!=JVMTI_ERROR_NONE||!signature.p)return Fail(error,"Target public signature unavailable");
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;
 std::vector<unsigned char> committed;
 if(provenance.Find(env,target,&committed)){
  if(!evolution::ReadDexDeclarations(committed,&schema,error))return false;
 }else{
  jclass type=env->GetObjectClass(input);auto id=env->GetFieldID(type,"dex","[[B");
  auto dex=static_cast<jobjectArray>(env->GetObjectField(input,id));std::vector<std::vector<unsigned char>> files;
  for(jsize i=0;dex&&i<env->GetArrayLength(dex);i++){
   auto array=static_cast<jbyteArray>(env->GetObjectArrayElement(dex,i));std::vector<unsigned char> bytes(env->GetArrayLength(array));
   env->GetByteArrayRegion(array,0,bytes.size(),reinterpret_cast<jbyte*>(bytes.data()));env->DeleteLocalRef(array);files.push_back(std::move(bytes));
  }
  env->DeleteLocalRef(type);if(dex)env->DeleteLocalRef(dex);
  // APK provenance additionally requires the application's defining loader.
  auto class_type=env->FindClass("java/lang/Class");
  auto loader_method=class_type?env->GetMethodID(class_type,"getClassLoader","()Ljava/lang/ClassLoader;"):nullptr;
  if(class_type)env->DeleteLocalRef(class_type);
  if(!loader_method||env->ExceptionCheck())return Fail(error,"Target loader oracle unavailable");
  auto owner_loader=env->CallObjectMethod(anchor,loader_method),target_loader=env->CallObjectMethod(target,loader_method);
  bool same=env->IsSameObject(owner_loader,target_loader);if(owner_loader)env->DeleteLocalRef(owner_loader);if(target_loader)env->DeleteLocalRef(target_loader);
  if(!same||env->ExceptionCheck()||!SelectInstalledClass(files,signature.p,&schema,&counts,error))return Fail(error,"Target lacks known installed-APK or successful-engine DEX provenance");
 }
 if(schema.descriptor!=signature.p)return Fail(error,"Target Class differs from DEX provenance");
 Roots owned{env,{}};auto owner=owned.Own(target);
 TargetRequest request{this,owner,&schema,{},{},{},
  reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod)),
  reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv")),
  reinterpret_cast<void*(*)(void*,jmethodID)>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID")),
  {}};
 request.field_proof_base=target_field_proofs.size();
 jint n=0;jmethodID* methods=nullptr;if(ti->GetClassMethods(target,&n,&methods)!=JVMTI_ERROR_NONE||size_t(n)!=schema.methods.size())return Fail(error,"Target methods disagree with provenance");
 struct Methods{jvmtiEnv* ti;jmethodID* p;~Methods(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release_methods{ti,methods};
 std::map<std::pair<std::string,std::string>,evolution::Member> expected;for(auto m:schema.methods)expected.emplace(std::make_pair(m.name,m.descriptor),m);
 for(jint i=0;i<n;i++){
  Text name{ti},descriptor{ti};jint modifiers=0;
  if(ti->GetMethodName(methods[i],&name.p,&descriptor.p,nullptr)!=JVMTI_ERROR_NONE||ti->GetMethodModifiers(methods[i],&modifiers)!=JVMTI_ERROR_NONE||!name.p||!descriptor.p)return false;
  auto member=expected.find({name.p,descriptor.p});if(member==expected.end()||(member->second.access&0xffffu)!=(uint32_t(modifiers)&0xffffu))return Fail(error,"Target method public metadata disagrees with provenance");
  auto mirror=env->ToReflectedMethod(target,methods[i],(modifiers&8)?JNI_TRUE:JNI_FALSE);auto root=owned.Own(mirror);if(mirror)env->DeleteLocalRef(mirror);
  request.methods.push_back({root,member->second.dex_index,uint32_t(modifiers)});request.ids.push_back(methods[i]);expected.erase(member);
 }
 jint f=0;jfieldID* fields=nullptr;if(ti->GetClassFields(target,&f,&fields)!=JVMTI_ERROR_NONE||size_t(f)!=schema.fields.size())return Fail(error,"Target fields disagree with provenance");
 struct Fields{jvmtiEnv* ti;jfieldID* p;~Fields(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release_fields{ti,fields};
 std::map<std::pair<std::string,std::string>,evolution::Member> expected_fields;for(auto field:schema.fields)expected_fields.emplace(std::make_pair(field.name,field.descriptor),field);
 std::vector<std::string> names(f),descriptors(f);
 for(jint i=0;i<f;i++){
  Text name{ti},descriptor{ti};jint modifiers=0;if(ti->GetFieldName(target,fields[i],&name.p,&descriptor.p,nullptr)!=JVMTI_ERROR_NONE||ti->GetFieldModifiers(target,fields[i],&modifiers)!=JVMTI_ERROR_NONE||!name.p||!descriptor.p)return false;
  auto member=expected_fields.find({name.p,descriptor.p});if(member==expected_fields.end()||(member->second.access&0x50dfu)!=(uint32_t(modifiers)&0x50dfu))return Fail(error,"Target field public metadata disagrees with provenance");
  names[i]=name.p;descriptors[i]=descriptor.p;auto mirror=env->ToReflectedField(target,fields[i],(modifiers&8)?JNI_TRUE:JNI_FALSE);auto root=owned.Own(mirror);if(mirror)env->DeleteLocalRef(mirror);
  auto proof=contextual_field_id::Prepare(env,root,fields[i],error);
  if(!proof)return false;
  target_field_proofs.push_back(std::move(proof));
  request.fields.push_back({root,names[i].c_str(),descriptors[i].c_str(),uint32_t(modifiers)});expected_fields.erase(member);
 }
 if(!request.decode_method||!request.method_leaf||!request.field_leaf||env->ExceptionCheck()||
    runnable_executor::With(env,owner,TargetObserve,&request)!=runnable_jni::Result::kSuccess){if(error)*error=request.error.empty()?"Target rooted native/public identity proof rejected":request.error;return false;}
 StagedClass stage{static_cast<jclass>(env->NewGlobalRef(target)),request.field_proof_base,{}, {}};
 if(!stage.owner||env->ExceptionCheck())return false;
 for(jint i=0;i<f;i++){stage.ids.push_back(fields[i]);auto mirror=env->NewGlobalRef(request.fields[i].strong_global_field);if(!mirror||env->ExceptionCheck()){for(auto m:stage.mirrors)env->DeleteGlobalRef(m);env->DeleteGlobalRef(stage.owner);return false;}stage.mirrors.push_back(mirror);}
 staged_classes.push_back(std::move(stage));return true;
}
bool NativeEngine::StageGraph(JNIEnv* env,jvmtiEnv* ti,const std::vector<Definition>& definitions,std::string* error){
 graph_current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 graph_field_leaf=reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 graph_add_global=reinterpret_cast<decltype(graph_add_global)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kAddGlobalRef));
 if(!graph_current||!graph_field_leaf||!graph_add_global)return Fail(error,"Paused graph native readers unavailable");
 graph_decode=reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so",abi::arm64::android14::kDecodeGlobalJObject));
 if(!graph_decode)graph_decode=reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject));
 graph_decode_method=reinterpret_cast<decltype(graph_decode_method)>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"));
 graph_decode_field=reinterpret_cast<decltype(graph_decode_field)>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID"));
 graph_same=reinterpret_cast<decltype(graph_same)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
 if(!graph_decode||!graph_decode_method||!graph_decode_field||!graph_same)return Fail(error,"Named source-owned graph readers unavailable");
 jint count=0;jclass* loaded=nullptr;
 if(ti->GetLoadedClasses(&count,&loaded)!=JVMTI_ERROR_NONE)return Fail(error,"Public candidate Class graph unavailable");
 struct Loaded {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Loaded(){for(jint i=0;i<count;++i)env->DeleteLocalRef(values[i]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} release{env,ti,count,loaded};
 for(jint i=0;i<count;++i){
  bool affected=false;for(const auto& definition:definitions)if(env->IsAssignableFrom(loaded[i],definition.klass))affected=true;
  if(env->ExceptionCheck())return Fail(error,"Public graph classification failed");
  if(!affected)continue;
  jint status=0;if(ti->GetClassStatus(loaded[i],&status)!=JVMTI_ERROR_NONE)return false;
  if(status&JVMTI_CLASS_STATUS_ERROR)continue;
  if(!(status&JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Candidate descendant is not prepared");
  if(!Preflight(env,ti,loaded[i],error))return false;
 }
 if(!StagedInputs(env,ti,definitions,this,error))return false;
 for(const auto& definition:definitions)for(const auto& stage:staged_classes)if(env->IsSameObject(definition.klass,stage.owner))graph_bases.push_back(stage.owner);
 if(graph_bases.size()!=definitions.size())return false;
 if(runnable_executor::With(env,anchor,ObserveGraphReaders,this)!=runnable_jni::Result::kSuccess)return Fail(error,"Native graph decoder differs from original rooted public field/Class oracles");
 if(schema_operation){
  native_schema_guard_verifier::Bindings bindings;bindings.vm=vm;bindings.metadata=&metadata;bindings.methods=&model;bindings.abstract_instances=&abstract_instances;bindings.owner=this;
  bindings.decode_global=DecodeGraph;bindings.decode_method=DecodeGraphMethod;bindings.decode_field=DecodeGraphField;bindings.method_class=GraphMethodClass;bindings.same_method=GraphSame;bindings.readable=GraphReadable;bindings.writable=GraphWritable;
  if(!schema_verifier.Stage(env,ti,graph_bases,bindings,error))return false;
 }return true;
}
bool NativeEngine::CheckGraph(JNIEnv* env,jvmtiEnv* ti,const std::vector<jclass>& actual,const native_owned_removal::RootedGraph& rooted,native_owned_removal::GraphWindow window,void* raw,std::string* error){
 auto& e=*static_cast<NativeEngine*>(raw);
 if(actual.size()!=e.staged_classes.size())return Fail(error,"Class loaded after candidate staging; retry whole batch");
 // The original pause exempts its defining thread. Re-enumerate registered
 // live Classes to catch children loaded by verification/allocation callbacks
 // after ART built oldClasses. This is metadata-only JNI/JVMTI, no Java calls.
 jint live_count=0;jclass* live=nullptr;
 if(ti->GetLoadedClasses(&live_count,&live)!=JVMTI_ERROR_NONE)return Fail(error,"Paused live Class closure unavailable");
 struct Live {JNIEnv* env;jvmtiEnv* ti;jint count;jclass* values;~Live(){for(jint n=0;n<count;++n)env->DeleteLocalRef(values[n]);if(values)ti->Deallocate(reinterpret_cast<unsigned char*>(values));}} live_scope{env,ti,live_count,live};
 size_t affected_count=0;
 for(jint n=0;n<live_count;++n){
  jint status=0;if(ti->GetClassStatus(live[n],&status)!=JVMTI_ERROR_NONE)return false;
  if(status&JVMTI_CLASS_STATUS_ERROR)continue;
  bool affected=false;for(auto base:e.graph_bases)if(env->IsAssignableFrom(live[n],base))affected=true;
  if(env->ExceptionCheck())return false;if(!affected)continue;
  if(!(status&JVMTI_CLASS_STATUS_PREPARED))return Fail(error,"Paused descendant is unresolved");
  size_t matches=0;for(auto klass:actual)matches+=env->IsSameObject(klass,live[n]);
  if(matches!=1)return Fail(error,"Defining thread created an unstaged descendant; retry whole batch");
  ++affected_count;
 }
 if(affected_count!=actual.size())return Fail(error,"Paused live graph disagrees with native rooted graph");
 // No FindClass, reflection calls, class initialization, or new class loading
 // here. The startup Direct lease prevents changes to these member tables.
 for(auto klass:actual){
  StagedClass* stage=nullptr;for(auto& candidate:e.staged_classes)if(env->IsSameObject(klass,candidate.owner)){if(stage)return Fail(error,"Duplicate staged Class identity");stage=&candidate;}
  if(!stage||env->ExceptionCheck())return Fail(error,"Actual paused graph contains an unstaged Class");
  jint count=0;jfieldID* fields=nullptr;
  if(ti->GetClassFields(klass,&count,&fields)!=JVMTI_ERROR_NONE)return false;
  struct Fields {jvmtiEnv* ti;jfieldID* p;~Fields(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}} release{ti,fields};
  if(size_t(count)!=stage->ids.size())return Fail(error,"Paused Class field count changed after staging");
  for(jint n=0;n<count;++n)if(std::count(stage->ids.begin(),stage->ids.end(),fields[n])!=1)return Fail(error,"Paused Class field ID changed after staging");
  for(size_t n=0;n<stage->mirrors.size();++n){
   void* mirror=nullptr;if(!DecodeGraph(&e,stage->mirrors[n],&mirror))return false;
   auto leaf=e.graph_field_leaf;
   if(!leaf||leaf(mirror)!=contextual_field_id::NativeField(e.target_field_proofs[stage->field_base+n]))return Fail(error,"Paused field native identity differs from public proof");
  }
  void* native=nullptr;if(!DecodeGraph(&e,stage->owner,&native))return false;
  field_array::View instance,statics;
  if(!e.metadata.Fields(native,false,&instance,error)||!e.metadata.Fields(native,true,&statics,error)||instance.selected_count+statics.selected_count!=stage->ids.size())return Fail(error,"Paused native field table count differs from complete public proof");
  for(size_t n=0;n<stage->ids.size();++n){uint32_t index=0;void* field=contextual_field_id::NativeField(e.target_field_proofs[stage->field_base+n]);
   if(!field||!(instance.IndexOf(field,&index)||statics.IndexOf(field,&index))||e.metadata.FieldDeclaringClass(field)!=native)return Fail(error,"Paused field proof is outside its native declaring table");}
 }
 if(e.schema_operation){
  const auto checkpoint=window==native_owned_removal::GraphWindow::Verified?native_schema_guard_verifier::Window::Verified:native_schema_guard_verifier::Window::Collected;
  if(!e.CaptureSchemaRoots(env,rooted,window,error)||!e.schema_verifier.CheckWindow(env,ti,actual,checkpoint,error))return false;
  if(window==native_owned_removal::GraphWindow::Collected){
   if(!e.RefreshMaps(error)||!e.RootedSchemaPlan(&e.prepared_schema_plan,error))return false;
   e.prepared_schema_service=PrepareCommit(e.prepared_schema_plan,&e,error);
   if(!e.prepared_schema_service||!e.prepared_schema_service->Validate(e.prepared_schema_plan))return false;
   const auto registrar=e.schema_registry.Collect();
   if(!rooted_schema_plan::CollectBindings(&e.prepared_schema_plan,registrar,&e.schema_plan_roots)||
      !e.provider.CollectManagedRoots(*e.prepared_schema_service,registrar)||
      !e.schema_verifier.CollectManagedRoots(registrar,error)||!e.schema_registry.Freeze())return Fail(error,"Collected schema root preparation failed");
  }
 }
 return true;
}
}
std::unique_ptr<Engine> Prepare(JNIEnv* env,jvmtiEnv* ti,jobject calibration,std::string* error){
 if(!env||!ti||!calibration||env->ExceptionCheck()){Fail(error,"Invalid production bootstrap caller");return nullptr;}
 auto engine=std::make_unique<NativeEngine>();auto& e=*engine;env->GetJavaVM(&e.vm);
 Roots roots{env,{}};
 jclass bootstrap=env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap");
 jmethodID create=bootstrap?env->GetStaticMethodID(bootstrap,"create","()Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;"):nullptr;
 jobject input=create?env->CallStaticObjectMethod(bootstrap,create):nullptr;
 if(bootstrap)env->DeleteLocalRef(bootstrap);
 if(!input||env->ExceptionCheck()){Fail(error,"Public production bootstrap inputs unavailable");return nullptr;}
 jobject global_input=roots.Own(input);env->DeleteLocalRef(input);
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;
 if(!OriginalDeclarations(env,global_input,&schema,&counts,error))return nullptr;
 jclass owner=env->GetObjectClass(calibration);if(!owner)return nullptr;
 Text owner_signature{ti};
 if(ti->GetClassSignature(owner,&owner_signature.p,nullptr)!=JVMTI_ERROR_NONE||
    !owner_signature.p||schema.descriptor!=owner_signature.p){
  env->DeleteLocalRef(owner);Fail(error,"Public calibration Class differs from installed APK oracle");return nullptr;
 }
 JNINativeMethod marker{const_cast<char*>("bootstrapNativeMarker"),const_cast<char*>("()V"),reinterpret_cast<void*>(Marker)};
 if(env->RegisterNatives(owner,&marker,1)!=JNI_OK||env->ExceptionCheck())return nullptr;
 jobject owner_root=roots.Own(owner);env->DeleteLocalRef(owner);
 jint count=0;jmethodID* ids=nullptr;
 if(ti->GetClassMethods(static_cast<jclass>(owner_root),&count,&ids)!=JVMTI_ERROR_NONE||count<3||size_t(count)!=schema.methods.size()){Fail(error,"Public declared methods disagree with installed APK class_data");return nullptr;}
 struct FreeMethods{jvmtiEnv* ti;jmethodID* ids;~FreeMethods(){if(ids)ti->Deallocate(reinterpret_cast<unsigned char*>(ids));}} free_methods{ti,ids};
 std::vector<method_identity::Oracle> method_oracles;method_identity::Oracle hot{};
 std::map<std::pair<std::string,std::string>,evolution::Member> expected;
 for(auto& m:schema.methods)expected.emplace(std::make_pair(m.name,m.descriptor),m);
 for(jint n=0;n<count;++n){Text name{ti},sig{ti};jint mods=0;
  if(ti->GetMethodName(ids[n],&name.p,&sig.p,nullptr)!=JVMTI_ERROR_NONE||ti->GetMethodModifiers(ids[n],&mods)!=JVMTI_ERROR_NONE||!name.p||!sig.p)return nullptr;
  auto entry=expected.find({name.p,sig.p});
  if(entry==expected.end()||(entry->second.access&0x11fu)!=(uint32_t(mods)&0x11fu)){Fail(error,"Public method identity disagrees with original DEX");return nullptr;}
  jobject reflected=env->ToReflectedMethod(static_cast<jclass>(owner_root),ids[n],(mods&8)?JNI_TRUE:JNI_FALSE);
  jobject root=roots.Own(reflected);if(reflected)env->DeleteLocalRef(reflected);
  if(!root||env->ExceptionCheck())return nullptr;
  method_identity::Oracle oracle{root,entry->second.dex_index,static_cast<uint32_t>(mods)};
  method_oracles.push_back(oracle);if(std::strcmp(name.p,"hotLoop")==0&&std::strcmp(sig.p,"(I)I")==0)hot=oracle;
  expected.erase(entry);
 }
 if(!expected.empty()||!hot.strong_global_method)return nullptr;
 jclass input_type=env->GetObjectClass(global_input);
 jfieldID storage_field=env->GetFieldID(input_type,"storage","Ljava/lang/Object;");
 jobject storage=storage_field?env->GetObjectField(global_input,storage_field):nullptr;
 jobject storage_root=roots.Own(storage);if(storage)env->DeleteLocalRef(storage);
 jclass storage_type=storage_root?env->GetObjectClass(storage_root):nullptr;
 jobject storage_class=roots.Own(storage_type);if(storage_type)env->DeleteLocalRef(storage_type);
 if(!storage_class||env->ExceptionCheck())return nullptr;
 jint field_count=0;jfieldID* fields=nullptr;
 if(ti->GetClassFields(static_cast<jclass>(storage_class),&field_count,&fields)!=JVMTI_ERROR_NONE||field_count!=28){Fail(error,"Production storage fixture requires all 28 original fields");return nullptr;}
 struct FreeFields{jvmtiEnv* ti;jfieldID* ids;~FreeFields(){if(ids)ti->Deallocate(reinterpret_cast<unsigned char*>(ids));}} free_fields{ti,fields};
 std::vector<std::string> names(field_count),types(field_count);std::vector<modern_field_metadata::FieldOracle> field_oracles;
 for(jint n=0;n<field_count;++n){Text name{ti},sig{ti};jint mods=0;
  if(ti->GetFieldName(static_cast<jclass>(storage_class),fields[n],&name.p,&sig.p,nullptr)!=JVMTI_ERROR_NONE||ti->GetFieldModifiers(static_cast<jclass>(storage_class),fields[n],&mods)!=JVMTI_ERROR_NONE||!name.p||!sig.p)return nullptr;
  names[n]=name.p;types[n]=sig.p;jobject reflected=env->ToReflectedField(static_cast<jclass>(storage_class),fields[n],(mods&8)?JNI_TRUE:JNI_FALSE);
  jobject root=roots.Own(reflected);if(reflected)env->DeleteLocalRef(reflected);if(!root||env->ExceptionCheck())return nullptr;
  field_oracles.push_back({root,names[n].c_str(),types[n].c_str(),static_cast<uint32_t>(mods)});
 }
 if(!e.hooks.Initialize(error)||!runtime_services::CalibrateRuntimeServices(e.hooks,env,&e.runtime,error)||!e.metadata.Bind(e.hooks,error))return nullptr;
 // The public Field oracle uses the independently calibrated Runnable carrier.
 // A fresh production process has no carrier before runtime service bootstrap.
 if(!contextual_field_id::Initialize(e.hooks,error))return nullptr;
 auto leaf=reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(e.hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 auto field_leaf=reinterpret_cast<field_mirror::NamedField>(e.hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 Request request{&e,owner_root,storage_class,&method_oracles,&field_oracles,counts,leaf,field_leaf,{}};
 if(!leaf||!field_leaf||runnable_executor::With(env,owner_root,Observe,&request)!=runnable_jni::Result::kSuccess){if(error)*error=request.error;return nullptr;}
 if(!e.storage.Calibrate(e.hooks,env,storage_class,storage_root,e.metadata,field_oracles.data(),field_oracles.size(),error))return nullptr;
 // Mandatory native commit services are checked before exposing an owner.
 const char* commit_symbols[]={"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE",
 "_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE",
 "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv",
 "_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv",
 "_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc"};
 for(auto symbol:commit_symbols)if(!e.hooks.FindSymbol("libart.so",symbol)){Fail(error,"Missing named production commit service");return nullptr;}
 if(!e.hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE")){Fail(error,"Missing named production heap replacement");return nullptr;}
 auto mh_id=env->GetFieldID(input_type,"methodHandles","[Ljava/lang/invoke/MethodHandle;");
 auto methods_id=env->GetFieldID(input_type,"methods","[Ljava/lang/reflect/Method;");
 auto handles=static_cast<jobjectArray>(env->GetObjectField(global_input,mh_id));
 auto reflected=static_cast<jobjectArray>(env->GetObjectField(global_input,methods_id));
 if(!handles||!reflected||env->ExceptionCheck()||env->GetArrayLength(handles)!=3||env->GetArrayLength(reflected)!=3)return nullptr;
 jobject method_roots[3];
 for(size_t i=0;i<3;i++){
  auto h=env->GetObjectArrayElement(handles,i);auto m=env->GetObjectArrayElement(reflected,i);
  auto hr=roots.Own(h);method_roots[i]=roots.Own(m);auto id=env->FromReflectedMethod(m);
  env->DeleteLocalRef(h);env->DeleteLocalRef(m);
  if(!hr||!method_roots[i]||!id||env->ExceptionCheck())return nullptr;
  e.method_handles.push_back({hr,id});
 }
 auto vh=[&](const char* name){auto id=env->GetFieldID(input_type,name,"Ljava/lang/invoke/VarHandle;");auto h=id?env->GetObjectField(global_input,id):nullptr;auto root=roots.Own(h);if(h)env->DeleteLocalRef(h);return root;};
 e.field_handles={{vh("instanceHandle"),nullptr},{vh("staticHandle"),nullptr}};
 jfieldID proof_ids[2]={env->GetFieldID(static_cast<jclass>(storage_class),"intValue","I"),env->GetStaticFieldID(static_cast<jclass>(storage_class),"staticIntValue","I")};
 e.original_field_values[0]=env->GetIntField(storage_root,proof_ids[0]);e.original_field_values[1]=env->GetStaticIntField(static_cast<jclass>(storage_class),proof_ids[1]);
 for(size_t i=0;i<2;i++){
  auto getter_id=env->GetFieldID(input_type,i?"staticGetter":"instanceGetter","Ljava/lang/invoke/MethodHandle;");
  auto getter=getter_id?env->GetObjectField(global_input,getter_id):nullptr;auto root=roots.Own(getter);if(getter)env->DeleteLocalRef(getter);
  if(!root||env->ExceptionCheck())return nullptr;e.field_method_handles.push_back({root,proof_ids[i],{},i?10u:8u});
 }
 jobject proof_fields[2];
 for(size_t i=0;i<2;i++){auto f=env->ToReflectedField(static_cast<jclass>(storage_class),proof_ids[i],i?JNI_TRUE:JNI_FALSE);proof_fields[i]=roots.Own(f);if(f)env->DeleteLocalRef(f);}
 for(size_t i=0;i<2;i++){
  auto proof=contextual_field_id::Prepare(env,proof_fields[i],proof_ids[i],error);if(!proof)return nullptr;
  e.field_method_handles[i].identity=proof;e.bootstrap_field_proofs.push_back(std::move(proof));
 }
 HandleRequest handle_request{&e,method_roots[0],{proof_fields[0],proof_fields[1]},leaf,field_leaf};
 if(env->ExceptionCheck()||!e.field_handles[0].strong_global||!e.field_handles[1].strong_global||
    runnable_executor::With(env,owner_root,ObserveHandles,&handle_request)!=runnable_jni::Result::kSuccess){Fail(error,"Production native field/ID handle oracles differ");return nullptr;}
 jclass class_type=env->FindClass("java/lang/Class");
 auto get_loader=class_type?env->GetMethodID(class_type,"getClassLoader","()Ljava/lang/ClassLoader;"):nullptr;
 if(class_type)env->DeleteLocalRef(class_type);
 auto loader=get_loader?env->CallObjectMethod(owner_root,get_loader):nullptr;auto loader_root=roots.Own(loader);if(loader)env->DeleteLocalRef(loader);
 jclass object=env->FindClass("java/lang/Object");
 auto hash=object?env->GetMethodID(object,"hashCode","()I"):nullptr;
 // Android's public Class.getClassLoader may return a BootClassLoader facade.
 // JVMTI reports the real defining loader and uses null for the bootstrap set.
 jobject boot_loader=nullptr;
 if(!object||ti->GetClassLoader(object,&boot_loader)!=JVMTI_ERROR_NONE||boot_loader){
  if(boot_loader)env->DeleteLocalRef(boot_loader);Fail(error,"Foreign allocator oracle must be a bootstrap Class");return nullptr;
 }
 auto boot_method=object&&hash?env->ToReflectedMethod(object,hash,JNI_FALSE):nullptr;auto boot_root=roots.Own(boot_method);
 if(boot_method)env->DeleteLocalRef(boot_method);if(object)env->DeleteLocalRef(object);
 AllocatorRequest allocator{&e,&method_oracles,&field_oracles,boot_root,leaf,field_leaf,{},{},{}};
 exclusive_owned_roots::Service stopped;jobject stop_roots[]={loader_root,owner_root,storage_class};
 if(!loader_root||!boot_root||env->ExceptionCheck()||
    runnable_executor::With(env,owner_root,CollectAllocator,&allocator)!=runnable_jni::Result::kSuccess||
    !exclusive_owned_roots::Service::Bind(e.hooks,&stopped,error)||
    !stopped.With(env,stop_roots,3,ProveAllocator,&allocator,error)){if(error&&!allocator.error.empty())*error=allocator.error;return nullptr;}
 e.input=global_input;
 auto target_id=env->GetFieldID(input_type,"additiveClass","Ljava/lang/Class;");auto dex_id=env->GetFieldID(input_type,"additiveDex","[B");
 auto target=target_id?static_cast<jclass>(env->GetObjectField(global_input,target_id)):nullptr;
 auto dex=dex_id?static_cast<jbyteArray>(env->GetObjectField(global_input,dex_id)):nullptr;
 if(!target||!dex||env->ExceptionCheck())return nullptr;
 std::vector<unsigned char> replacement(static_cast<size_t>(env->GetArrayLength(dex)));
 env->GetByteArrayRegion(dex,0,static_cast<jsize>(replacement.size()),reinterpret_cast<jbyte*>(replacement.data()));
 runtime_services::View jit;
 if(env->ExceptionCheck()||!owned_jit_receivers::CalibrateJitReceivers(e.hooks,env,owner_root,hot,e.model,e.runtime,target,replacement.data(),replacement.size(),&jit,error))return nullptr;
 e.runtime.receivers=jit;e.anchor=owner_root;
 // Calibration retains the defining Classes, not all original APK byte arrays
 // and temporary reflection/storage objects. ART native members remain alive
 // through these process-owned strong Class roots.
 for(auto& root:roots.values)if(root==owner_root||root==storage_class||root==global_input||root==e.method_handles[0].strong_global||root==e.method_handles[1].strong_global||root==e.method_handles[2].strong_global||root==e.field_handles[0].strong_global||root==e.field_handles[1].strong_global||root==e.field_handles[0].reflected_field||root==e.field_handles[1].reflected_field||root==e.field_method_handles[0].strong_global||root==e.field_method_handles[1].strong_global){e.roots.push_back(root);root=nullptr;}
 return engine;
}
}

// Ordinary JNI proof used before a fresh retained ID is exercised. In particular,
// the contextual service rejects an opaque-ID collision with a live retired alias.
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_internal_ProductionBootstrap_verifyLiveFieldId(
    JNIEnv* env,jclass,jobject field){
 std::string error;
 jfieldID id=field?env->FromReflectedField(field):nullptr;
 auto proof=artdcevm::contextual_field_id::Prepare(env,field,id,&error);
 const bool ready=bool(proof);
 artdcevm::contextual_field_id::Release(env,&proof);
 if(env->ExceptionCheck())return nullptr;
 return ready?nullptr:env->NewStringUTF(error.empty()?"Fresh field ID identity could not be proven":error.c_str());
}
