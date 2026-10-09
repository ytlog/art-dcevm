#include "platform/common/source_cache_resolution.h"
#include "platform/common/production_bootstrap.h"
#include "platform/common/owned_failure_diagnostic.h"
#include "platform/common/bootstrap_dex_oracle.h"
#include "platform/common/ordinary_source_fixture.h"
#include "platform/common/source_cache_method_witness.h"
#include "platform/common/source_cache_field_witness.h"
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
#include "platform/common/native_source_calibration_bridge.h"
#include "platform/common/native_source_observation_owner.h"
#include "platform/common/native_schema_run_state.h"
#include "platform/common/native_production_schema_capabilities.h"
#include "platform/common/frame_family_calibration.h"
#include "platform/common/compiled_frame_owner.h"
#include "platform/common/compiled_frame_ordinary_context.h"
#include "platform/common/thread_state_calibration.h"
#include "platform/common/rooted_schema_plan.h"
#include "platform/common/prepared_root_registry.h"
#include "platform/abi/arm64/android-common/jni_global_ref.h"
#include "platform/common/redefinition_gate.h"
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/rooted_field_identities.h"
#include "platform/common/native_final_field_service.h"
#include "platform/common/native_final_field_calibration.h"
#include "platform/common/native_reference_decoder.h"
#include "platform/common/native_removal_policy.h"
#include "platform/common/schema_snapshot.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include "platform/common/native_structural_reflection.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/native_scope_executor.h"
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
 ~NativeEngine(){JNIEnv* env=nullptr;if(vm&&vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK){provenance.Clear(env);ClearFieldProofs(env);if(source_cache_witness.cache)env->DeleteGlobalRef(source_cache_witness.cache);for(auto& p:bootstrap_field_proofs)contextual_field_id::Release(env,&p);contextual_field_id::Prune(env,retired_fields);for(auto p:roots)env->DeleteGlobalRef(p);}}
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
 std::vector<native_hierarchy_graph::Input> schema_inputs;
 abi::arm64::android11::CurrentThread graph_current=nullptr;
 field_mirror::NamedField graph_field_leaf=nullptr;
 void ClearStagedClasses(JNIEnv* env){schema_inputs.clear();graph_bases.clear();for(auto& c:staged_classes){for(auto mirror:c.mirrors)env->DeleteGlobalRef(mirror);env->DeleteGlobalRef(c.owner);}staged_classes.clear();}
 void ClearFieldProofs(JNIEnv* env){graph_fields.Clear(env);graph_staging=false;ClearSchemaRoots(env);ClearStagedClasses(env);for(auto& proof:target_field_proofs)contextual_field_id::Release(env,&proof);target_field_proofs.clear();contextual_field_id::Prune(env,retired_fields);}
 native_cache::Service cache;
 native_source_stop::Service source_stop;
 native_source_observation::Owner source_observers;
 native_production_schema_capabilities::Owner source_capabilities;
 std::string source_factory_diagnostic;bool source_calibration_started=false;
 bool SourceCapabilitiesCalibrated()const override{return source_capabilities.Ready();}
 bool FrameGeometryCalibrated()const override{return frame_geometry_proven;}
 bool CompiledFramesCalibrated()const override{return compiled_frames_proven;}
 std::string FrameCalibrationDiagnostic()const override{return compiled_frames_proven?"":frame_geometry_diagnostic.empty()?"compiled-frame-calibration-not-attempted":frame_geometry_diagnostic;}
 std::string SourceFactoryDiagnostic()const override{
  return !source_factory_diagnostic.empty()?source_factory_diagnostic:source_capabilities.Ready()?"":source_capabilities.Diagnostic();
 }
 jobject source_fixture=nullptr;JNIEnv* source_env=nullptr;jvmtiEnv* source_ti=nullptr;
 frame_family_calibration::Service frame_calibrator;
 frame_family_calibration::Service::Prepared frame_calibration_storage;
 std::vector<frame_family_calibration::MethodOracle> frame_methods;
 whole_process_frames::Service whole_frames;bool frame_geometry_proven=false,compiled_frames_proven=false;std::string frame_geometry_diagnostic;
 const native_source_stop::Session* calibration_stop=nullptr;void* calibration_self=nullptr;
 struct CompiledRequest {NativeEngine* engine;compiled_frame_fixture::Owner* fixture;frame_family_calibration::Result result;std::string error;abi::arm64::android11::CurrentThread current_thread=nullptr;};
 struct CalibrationRoots final:source_handle_roots::StoppedReader {
  const native_source_stop::Session& stop;void* self;std::array<const void*,32> slots{};size_t count=0;
  CalibrationRoots(const native_source_stop::Session& s,void* thread):stop(s),self(thread){}
  bool FinalRootWindow()const noexcept override{return stop.IsStopped(self);}
  bool ReadStoppedRoot(const void* slot,void** output)const noexcept override{
   if(!output||!FinalRootWindow())return false;
   for(size_t n=0;n<count;++n)if(slots[n]==slot){uint32_t value=0;std::memcpy(&value,slot,sizeof(value));*output=reinterpret_cast<void*>(uintptr_t(value));return true;}
   return false;
  }
 };
 static bool CompiledReadable(const void* address,size_t* extent){
  auto* e=current;return e&&e->calibration_stop&&e->calibration_stop->IsStopped(e->calibration_self)&&CachedExtent(e,address,extent);
 }
 struct CompiledScan {CompiledRequest& request;CalibrationRoots& roots;void* self;bool attempted=false;bool threw=false;bool mappings=false;};
 static bool CalibrateCompiledStopped(void* raw,native_source_stop::Session& stopped) noexcept {
  auto& scan=*static_cast<CompiledScan*>(raw);auto& r=scan.request;auto& e=*r.engine;
  if(!stopped.IsStopped(scan.self)||!scan.roots.FinalRootWindow())return false;
  scan.attempted=true;e.calibration_stop=&stopped;e.calibration_self=scan.self;
  struct ClearStop{NativeEngine& e;~ClearStop(){e.calibration_stop=nullptr;e.calibration_self=nullptr;}} clear{e};
  try{
   scan.mappings=e.RefreshStoppedMappings(nullptr);
   return scan.mappings&&r.fixture->CalibrateStopped(stopped,scan.self,e.runtime.receivers.jit_code_cache,scan.roots,CompiledReadable,&r.result);
  }catch(...){scan.threw=true;return false;}
 }
 static bool CalibrateCompiled(const runnable_jni::Context& context,void* raw){
  auto& r=*static_cast<CompiledRequest*>(raw);auto& e=*r.engine;
  if(!compiled_frame_fixture::SameOrdinaryOwner(context,e.source_env,static_cast<JavaVM*>(e.vm),r.current_thread)||!e.source_stop.Calibrated())return Fail(&r.error,"Compiled frame calibration callback differs from actual VM/environment/Thread/source owner");
  source_handle_roots::Pool pool(e.source_capabilities.Roots());
  if(!r.fixture->RegisterRoots(context,pool,&r.error))return false;
  size_t extent=0;if(!method_identity::ReadableMappingExtent(context.self,&extent))return Fail(&r.error,"Compiled frame current Thread mapping unavailable");
  if(!r.fixture->Start(context.env,&r.error))return false;
  bool proven=false;bool threw=false;bool mapping_failed=false;
  {native_source_stop::Session stopped(e.source_stop);
   CalibrationRoots roots(stopped,context.self);
   if(!pool.VisitUsedSlots([&](const void* slot){if(roots.count==roots.slots.size())return false;roots.slots[roots.count++]=slot;return true;})){
    r.fixture->Close(context.env,&r.error);return false;
   }
   CompiledScan scan{r,roots,context.self};
   if(e.source_stop.RequiresOriginalWaiting()){
    proven=stopped.InspectOriginalWaiting(context.self,extent,&scan,CalibrateCompiledStopped);
   }else if(stopped.Begin(context.self,extent)){
    proven=CalibrateCompiledStopped(&scan,stopped);
   }
   threw=scan.threw;mapping_failed=scan.attempted&&!scan.mappings;
   if(!proven&&!scan.attempted)r.error=stopped.Failure();
  } // Actual resume/FinishGC/RestoreRunnable happen before any Java close.
  const bool closed=r.fixture->Close(context.env,&r.error);
  if(mapping_failed)r.error="Compiled frame stopped mapping snapshot rejected";
  if(threw)r.error="Compiled frame stopped inspection threw";
  if(!proven&&r.error.empty())r.error=r.result.failure?r.result.failure:"Compiled frame original map/value oracle rejected";
  return proven&&closed&&r.result.compiled_ready;
 }
 bool PrepareCompiledFrames(JNIEnv* env,jvmtiEnv* ti){
  if(compiled_frames_proven)return true;
  if(!source_capabilities.Roots().Calibrated())return Fail(&frame_geometry_diagnostic,"compiled-frame-calibration-awaiting-source-roots");
  if(!source_stop.Calibrated())return Fail(&frame_geometry_diagnostic,"compiled-frame-calibration-awaiting-source-stop");
  if(!env||!ti||env->ExceptionCheck()||schema_run||graph_staging||(current&&current!=this))return Fail(&frame_geometry_diagnostic,"compiled-frame-calibration-requires-ordinary-unstaged-owner");
  // Priming may already have acquired the independent source proof in an
  // earlier operation. Borrow a fresh ordinary context for this retry; never
  // rely on the finished operation's JNIEnv or transient current engine.
  struct Context {NativeEngine& e;NativeEngine* previous;JNIEnv* env;jvmtiEnv* ti;
   explicit Context(NativeEngine& owner,JNIEnv* now,jvmtiEnv* jvmti):e(owner),previous(current),env(owner.source_env),ti(owner.source_ti){current=&owner;e.source_env=now;e.source_ti=jvmti;}
   ~Context(){e.source_env=env;e.source_ti=ti;current=previous;}
  } context{*this,env,ti};
  frame_geometry_diagnostic="compiled-frame-calibration-preparing-public-fixture";
  if(!PrepareStoppedMaps(&frame_geometry_diagnostic))return false;
  jclass type=env->GetObjectClass(input);jfieldID id=type?env->GetFieldID(type,"dex","[[B"):nullptr;
  auto dex=id?static_cast<jobjectArray>(env->GetObjectField(input,id)):nullptr;if(type)env->DeleteLocalRef(type);
  if(!dex||env->ExceptionCheck())return Fail(&frame_geometry_diagnostic,"compiled-frame-calibration-installed-dex-input-unavailable");
  auto fixture=compiled_frame_fixture::Owner::Prepare(hooks,env,ti,dex,&frame_geometry_diagnostic);env->DeleteLocalRef(dex);
  if(!fixture)return false;
  auto current_thread=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
  if(!current_thread)return Fail(&frame_geometry_diagnostic,"compiled-frame-calibration-current-thread-leaf-unavailable");
  CompiledRequest request{this,fixture.get(),{}, {},current_thread};
  if(runnable_executor::With(env,anchor,CalibrateCompiled,&request)!=runnable_jni::Result::kSuccess){frame_geometry_diagnostic=request.error.empty()?"compiled-frame-calibration-runnable-callback-not-completed":request.error;return false;}
  auto finder=[&](const char* symbol){return hooks.FindSymbol("libart.so",symbol);};
  compiled_frames_proven=whole_process_frames::Service::Bind(request.result.scanner_family,finder,&whole_frames);
  if(!compiled_frames_proven)frame_geometry_diagnostic="Full proved compiled scanner binding rejected";return compiled_frames_proven;
 }
 native_required_cleanup::Binding cleanup_binding;
 native_schema_run_state::Dependencies schema_dependencies;
 static void CalibrationBegin(void* driver,const runnable_jni::Context& context,void* raw) noexcept {
  auto& e=*static_cast<NativeEngine*>(raw);e.source_calibration_started=false;
  try{e.source_calibration_started=e.source_capabilities.Begin(driver,context,e.source_fixture,&e.source_factory_diagnostic);}
  catch(...){e.source_calibration_started=false;}
 }
 static void CalibrationEnd(void* driver,bool success,void* raw) noexcept {
  auto& e=*static_cast<NativeEngine*>(raw);if(!e.source_calibration_started)return;
  try{if(e.source_capabilities.End(driver,success,&e.source_factory_diagnostic))
    e.source_factory_diagnostic=e.source_capabilities.Ready()?"":
      e.source_capabilities.Allocator().ObservationDiagnostic()[0]?e.source_capabilities.Allocator().ObservationDiagnostic():e.source_capabilities.Diagnostic();}catch(...){}
  e.source_calibration_started=false;
 }
 bool PrepareFrameGeometry(const thread_suspend_service::Service& state,void* self,std::string* error){
  if(frame_geometry_proven)return true;
  auto finder=[&](const char* name){return hooks.FindSymbol("libart.so",name);};
  if(!frame_calibration_storage.context&&(!frame_family_calibration::Service::Bind(finder,&frame_calibrator)||
     !frame_calibrator.Prepare(&frame_calibration_storage)))return Fail(error,"Frame geometry preparation unavailable");
  frame_family_calibration::Result result;
  {thread_suspend_service::SuspendScope stopped(state,"art-dcevm independent frame geometry");
   if(!stopped.Supported()||!frame_calibrator.ObserveGeometry(self,frame_methods.data(),frame_methods.size(),frame_calibration_storage,&result))return Fail(error,"Actual constructor/public method geometry oracle rejected");}
  frame_geometry_proven=whole_process_frames::Service::BindGeometryOnly(result.scanner_family,finder,&whole_frames);
  return frame_geometry_proven||Fail(error,"Calibrated geometry scanner binding unavailable");
 }
 struct PublicCacheWitness {
  jobject cache=nullptr,klass=nullptr,field=nullptr,method=nullptr;
  InstalledClassIds ids;uint32_t field_index=UINT32_MAX,method_index=UINT32_MAX;
  uint32_t public_field_modifiers=0;
  void* native_field=nullptr;void* native_method=nullptr;
 } source_cache_witness;
 source_cache_resolution::Service cache_resolver;
 static bool ResolveCacheWitness(void*,const runnable_jni::Context&,source_handle_roots::Pool&,void*,void*,std::string*);
 bool PrepareSourceCacheWitness(JNIEnv*,std::string*);
 native_schema_run_state::CacheCalibration CacheCalibration()const {
  const auto& w=source_cache_witness;native_schema_run_state::CacheCalibration c;
  c.strong_cache=w.cache;c.strong_expected_class=w.klass;c.extent=112;
  c.counts={w.ids.type_count,w.ids.field_count,w.ids.method_count};
  c.type_index=w.ids.type_index;c.field_index=w.field_index;c.method_index=w.method_index;
  c.native_field=w.native_field;c.native_method=w.native_method;
  c.resolution_owner=const_cast<NativeEngine*>(this);c.resolve=ResolveCacheWitness;return c;
 }
 native_scope_executor::Executor native_scope;
 bool NativeScopeCalibrated()const override{return native_scope.Ready();}
 std::string source_stop_diagnostic;
 bool SourceStopCalibrated()const override{return source_stop.Calibrated();}
 std::string SourceStopDiagnostic()const override {
  return source_stop.Calibrated()?"":source_stop_diagnostic.empty()?"ordinary-source-lifecycle-not-observed":source_stop_diagnostic;
 }
 native_structural_reflection::Service reflection;
 native_member_provider::Service provider;
 native_schema_guard_verifier::Verifier schema_verifier;
 std::unique_ptr<native_schema_run_state::RunState> schema_run;
 native_abstract_instances::Service abstract_instances;
 // Owned by the process backend, never a temporary bootstrap candidate. A
 // partially installed listener keeps its Service and decoder owner alive.
 native_reference_decoder::Decoder permission_decoder;
 exclusive_owned_roots::Service permission_exclusive;
 std::unique_ptr<final_field_service::Service> permission_service;
 bool permission_decoder_bound=false,permissions_proven=false;std::string permission_diagnostic;
 using ThrowException=void (*)(void*,const char*,const char*);
 ThrowException permission_raise=nullptr;
 std::vector<native_schema_policy::Plan> schema_policies;
 bool schema_operation=false,schema_collected=false,graph_decoder_proven=false,graph_staging=false;
 std::string graph_reader_error;
 native_owned_removal::RootedGraph schema_roots;
 JNIEnv* schema_root_env=nullptr;
 abi::arm64::android_common::AddGlobalRef graph_add_global=nullptr;
 static jobject RootManaged(void* raw,void* object){auto& e=*static_cast<NativeEngine*>(raw);if(!object||!e.graph_add_global||!redefinition_gate::Owned(e.vm))return nullptr;return e.graph_add_global(e.runtime.receivers.java_vm,e.graph_current(),{uintptr_t(object)});}
 static void ReleaseManaged(void* raw,jobject root){auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_root_env)std::abort();e.schema_root_env->DeleteGlobalRef(root);}
 prepared_roots::Registry schema_registry{{this,RootManaged,DecodeGraph,ReleaseManaged}};
 source_commit::Plan prepared_schema_plan;
 bool schema_finalizing=false;
 using DecodeObject=abi::arm64::android11::ReleaseObjectPointer (*)(const void*,jobject);
 DecodeObject graph_decode=nullptr;
 void* (*graph_decode_method)(void*,jmethodID)=nullptr;
 jclass bootstrap_field_owner=nullptr;
 static bool FieldScope(void*,JNIEnv* env,jobject root,runnable_jni::Operation op,void* data){return runnable_executor::With(env,root,op,data)==runnable_jni::Result::kSuccess;}
 static bool FieldOracle(void*,const runnable_jni::Context& scope,jobject root,void** out){return runnable_executor::DecodeStrongGlobal(scope,root,out);}
 static bool FieldRoot(void* raw,jobject root,void** out){auto& e=*static_cast<NativeEngine*>(raw);if(!root||!out||!e.graph_decode||!e.graph_current||!redefinition_gate::Owned(e.vm))return false;*out=reinterpret_cast<void*>(e.graph_decode(e.graph_current(),root).reference);return *out!=nullptr;}
 static void* FieldLeaf(void* raw,void* mirror){auto& e=*static_cast<NativeEngine*>(raw);return e.graph_field_leaf?e.graph_field_leaf(mirror):nullptr;}
 static void* FieldOwner(void* raw,void* field){return static_cast<NativeEngine*>(raw)->metadata.FieldDeclaringClass(field);}
 // Fixed ObjectArray<HeapReference<Object>>: 8-byte Object, int32 length,
 // 4-byte elements. Every staged slot is independently checked against JNI.
 static void* FieldElement(void* raw,void* array,size_t index){if(index>=size_t(rooted_field_identities::Store::kChunkSize))return nullptr;return static_cast<NativeEngine*>(raw)->metadata.Reference(array,12+index*4);}
 rooted_field_identities::Store graph_fields{{this,FieldScope,FieldOracle,FieldRoot,FieldLeaf,FieldOwner,FieldElement}};
 static bool StageGraphField(void* raw,JNIEnv* env,jclass owner,jfieldID id,bool statik,std::string* error){return static_cast<NativeEngine*>(raw)->graph_fields.Stage(env,owner,id,statik,error);}
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
 // Reserve ordinary storage before entering the actual source stop. The
 // stopped refresh below never grows a container or invokes buffered stdio.
 bool PrepareStoppedMaps(std::string* error){
  if(!RefreshMaps(error))return false;
  if(graph_maps.size()>16384)return Fail(error,"Too many runtime mappings for source stop");
  graph_maps.reserve(std::min(size_t(16384),graph_maps.size()+4096));return true;
 }
 static bool RefreshStoppedMaps(void* raw,std::string* error){
  auto& e=*static_cast<NativeEngine*>(raw);
  auto* run=native_schema_run_state::RunState::Current();
  if(!run||run!=e.schema_run.get()||!run->StoppedWindow())return Fail(error,"Mapping refresh lacks actual schema stop owner");
  return e.RefreshStoppedMappings(error);
 }
 bool RefreshStoppedMappings(std::string* error){
  auto& e=*this;const bool owned_schema=schema_run&&native_schema_run_state::RunState::Current()==schema_run.get()&&schema_run->StoppedWindow();
  const bool owned_fixture=calibration_stop&&calibration_stop->IsStopped(calibration_self);
  if(!owned_schema&&!owned_fixture)return Fail(error,"Mapping refresh requires an actual independent native stop");
  const int fd=::open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return Fail(error,"Stopped mapping open failed");
  struct Close{int fd;~Close(){::close(fd);}} close{fd};
  e.graph_maps.clear();char buffer[4096],line[1024];size_t used=0;bool valid=true;
  auto consume=[&](){
   size_t cursor=0;uintptr_t begin=0,end=0;
   auto hex=[&](uintptr_t* out){size_t digits=0;uintptr_t value=0;
    while(cursor<used){const unsigned char ch=line[cursor];const unsigned digit=ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:16;
     if(digit==16)break;if(value>(UINTPTR_MAX-digit)/16)return false;value=value*16+digit;++cursor;++digits;}
    *out=value;return digits!=0;};
   if(!hex(&begin)||cursor>=used||line[cursor++]!='-'||!hex(&end)||cursor>=used||line[cursor++]!=' '||end<=begin||cursor+4>used)return false;
   if(line[cursor]!='r')return true;
   if(e.graph_maps.size()==e.graph_maps.capacity()||(!e.graph_maps.empty()&&begin<e.graph_maps.back().end))return false;
   e.graph_maps.push_back({begin,end,line[cursor+1]=='w'});return true;
  };
  while(valid){const ssize_t count=::read(fd,buffer,sizeof(buffer));if(count<0){valid=false;break;}if(count==0)break;
   for(ssize_t n=0;n<count;++n){if(buffer[n]=='\n'){if(!consume()){valid=false;break;}used=0;}else if(used==sizeof(line)){valid=false;break;}else line[used++]=buffer[n];}}
  if(valid&&used)valid=consume();
  if(!valid||e.graph_maps.empty()||!(owned_schema?schema_run->StoppedWindow():calibration_stop->IsStopped(calibration_self))){e.graph_maps.clear();return Fail(error,"Stopped mapping bounds/capacity rejected");}
  return true;
 }
 static bool GraphExtent(void* raw,const void* address,size_t* extent){
  auto& e=*static_cast<NativeEngine*>(raw);auto* run=native_schema_run_state::RunState::Current();
  if(!extent||!run||run!=e.schema_run.get()||!run->StoppedWindow())return false;
  return CachedExtent(raw,address,extent);
 }
 static bool CachedExtent(void* raw,const void* address,size_t* extent){
  const auto& e=*static_cast<NativeEngine*>(raw);if(!extent)return false;
  uintptr_t p=uintptr_t(address);
#if defined(__ANDROID__) && defined(__aarch64__)
  p&=UINT64_C(0x00ffffffffffffff);
#endif
  if(!p)return false;
  const auto it=std::upper_bound(e.graph_maps.begin(),e.graph_maps.end(),p,[](uintptr_t value,const auto& range){return value<range.begin;});
  if(it==e.graph_maps.begin())return false;const auto& range=*std::prev(it);
  if(p<range.begin||p>=range.end)return false;*extent=range.end-p;return true;
 }
 static bool GraphReadable(void* raw,const void* address,size_t bytes){
  const auto& e=*static_cast<NativeEngine*>(raw);
  if(e.schema_run&&native_schema_run_state::RunState::Current()==e.schema_run.get()&&e.schema_run->StoppedWindow()){
   size_t extent=0;return GraphExtent(raw,address,&extent)&&extent>=bytes;
  }
  if(!e.schema_finalizing)return GraphMapped(raw,address,bytes,false);
  size_t extent=0;return method_identity::ReadableMappingExtent(address,&extent)&&extent>=bytes;
 }
 static bool GraphWritable(void* raw,const void* address,size_t bytes){return GraphMapped(raw,address,bytes,true);}
 static bool ObserveGraphReaders(const runnable_jni::Context& scope,void* raw){
  auto& e=*static_cast<NativeEngine*>(raw);e.graph_decoder_proven=false;e.graph_reader_error.clear();void* expected=nullptr;
  const auto fail=[&](const char* boundary,size_t index=0){e.graph_reader_error=std::string("Native graph reader proof failed: ")+boundary+" index="+std::to_string(index);return false;};
  if(!runnable_executor::DecodeStrongGlobal(scope,e.anchor,&expected))return fail("public anchor strong oracle");
  if(e.graph_decode(scope.self,e.anchor).reference!=uintptr_t(expected))return fail("named anchor strong decoder");
  size_t fields=0;
  if(e.method_handles.empty()||!e.proof_method)return fail("missing bootstrap method oracle");
  if(e.graph_decode_method(e.runtime.receivers.jni_id_manager,e.method_handles[0].method)!=e.proof_method)return fail("bootstrap method ID decoder");
  for(size_t i=0;i<e.field_method_handles.size();++i){const auto& oracle=e.field_method_handles[i];
   if(!oracle.identity)return fail("missing bootstrap field oracle",i);
   if(e.graph_fields.Read(e.bootstrap_field_owner,oracle.field)!=contextual_field_id::NativeField(oracle.identity))return fail("bootstrap field ID decoder",i);++fields;
  }
  for(size_t c=0;c<e.staged_classes.size();++c){const auto& stage=e.staged_classes[c];void* owner=nullptr;
   if(!runnable_executor::DecodeStrongGlobal(scope,stage.owner,&owner))return fail("public staged Class strong oracle",c);
   if(e.graph_decode(scope.self,stage.owner).reference!=uintptr_t(owner))return fail("named staged Class strong decoder",c);
   if(stage.field_base>e.target_field_proofs.size()||stage.ids.size()>e.target_field_proofs.size()-stage.field_base)return fail("staged field proof range",c);
   for(size_t i=0;i<stage.ids.size();++i){void* native=e.graph_fields.Read(stage.owner,stage.ids[i]);
    if(native!=contextual_field_id::NativeField(e.target_field_proofs[stage.field_base+i]))return fail("staged field ID decoder",stage.field_base+i);++fields;
   }
  }
  e.graph_decoder_proven=fields>0;return e.graph_decoder_proven||fail("empty field oracle set");
 }
 static bool DecodeGraph(void* raw,jobject global,void** value){
  auto& e=*static_cast<NativeEngine*>(raw);if(!global||!value||!e.graph_decoder_proven||!e.graph_decode||!e.graph_current||!redefinition_gate::Owned(e.vm))return false;
  *value=reinterpret_cast<void*>(e.graph_decode(e.graph_current(),global).reference);return *value!=nullptr;
 }
 static void* DecodeGraphMethod(void* raw,jmethodID id){auto& e=*static_cast<NativeEngine*>(raw);return e.graph_decode_method(e.runtime.receivers.jni_id_manager,id);}
 static void* DecodeGraphField(void* raw,jclass owner,jfieldID id){return static_cast<NativeEngine*>(raw)->graph_fields.Read(owner,id);}
 static void* DecodeNativeGraphField(void* raw,void* owner,jfieldID id){return static_cast<NativeEngine*>(raw)->graph_fields.ReadNative(owner,id);}
 static void* GraphMethodClass(void* raw,void* method){return static_cast<NativeEngine*>(raw)->metadata.FieldDeclaringClass(method);}
 static bool GraphSame(void* raw,void* a,void* b){return static_cast<NativeEngine*>(raw)->graph_same(a,b);}
 void ClearSchemaRoots(JNIEnv* env){
  schema_root_env=env;if(permission_service)permission_service->Clear(env);prepared_schema_plan={};schema_registry.Clear();schema_finalizing=false;
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
 struct PermissionRunnable {NativeEngine* engine;JNIEnv* env;runnable_jni::Operation operation;void* payload;};
 static bool PermissionRunnableEnter(const runnable_jni::Context& scope,void* raw){auto& r=*static_cast<PermissionRunnable*>(raw);
  return redefinition_gate::Owned(r.engine->vm)&&scope.env==r.env&&scope.vm==r.engine->vm&&scope.self==r.engine->graph_current()&&r.operation(scope,r.payload);
 }
 static bool PermissionNativeScope(void* raw,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* payload){
  auto& e=*static_cast<NativeEngine*>(raw);if(!operation||!redefinition_gate::Owned(e.vm)||!e.RefreshMaps(&e.permission_diagnostic))return false;
  PermissionRunnable request{&e,env,operation,payload};return runnable_executor::With(env,root,PermissionRunnableEnter,&request)==runnable_jni::Result::kSuccess;
 }
 struct PermissionStop {NativeEngine* engine;JNIEnv* env;runnable_jni::Operation operation;void* payload;};
 static bool PermissionStopped(const exclusive_owned_roots::Context& stopped,void* raw){
  auto& r=*static_cast<PermissionStop*>(raw);if(!redefinition_gate::Owned(r.engine->vm)||stopped.count!=1||!stopped.roots[0])return false;
  // self comes from the original SuspendAll scope, rechecked against the
  // native getter; JNI/VM are the independently observed public receivers.
  if(r.engine->graph_current()!=stopped.self)return false;
  return r.operation({stopped.self,r.env,r.engine->vm},r.payload);
 }
 static bool PermissionExclusiveScope(void* raw,JNIEnv* env,jobject root,runnable_jni::Operation operation,void* payload){
  auto& e=*static_cast<NativeEngine*>(raw);JavaVM* actual=nullptr;
  if(!operation||!env||env->GetJavaVM(&actual)!=JNI_OK||actual!=e.vm||!redefinition_gate::Owned(e.vm)||!e.RefreshMaps(&e.permission_diagnostic))return false;
  PermissionStop request{&e,env,operation,payload};return e.permission_exclusive.With(env,&root,1,PermissionStopped,&request,&e.permission_diagnostic);
 }
 static void* PermissionThread(void* raw){return static_cast<NativeEngine*>(raw)->graph_current();}
 static bool PermissionOracle(void*,const runnable_jni::Context& scope,jobject root,void** out){return runnable_executor::DecodeStrongGlobal(scope,root,out);}
 static bool PermissionGlobal(void* raw,jobject root,void** out){auto& e=*static_cast<NativeEngine*>(raw);
  return e.permission_decoder.Strong({e.graph_current(),e.schema_root_env,e.vm},native_reference_decoder::Scope::NoSuspend,root,out);}
 static bool PermissionWeak(void* raw,void* thread,jweak root,void** out){return static_cast<NativeEngine*>(raw)->permission_decoder.Weak(thread,native_reference_decoder::Scope::Runnable,root,out);}
 static bool RegisterPermissionStrong(void* raw,JNIEnv* env,jobject root,std::string* error){return static_cast<NativeEngine*>(raw)->permission_decoder.RegisterStrong(env,root,error);}
 static bool RegisterPermissionWeak(void* raw,JNIEnv* env,jweak root,std::string* error){return static_cast<NativeEngine*>(raw)->permission_decoder.RegisterWeak(env,root,error);}
 static void ForgetPermissionRoot(void* raw,jobject root){static_cast<NativeEngine*>(raw)->permission_decoder.Forget(root);}
 static void ForgetPermissionWeak(void* raw,jweak root){ForgetPermissionRoot(raw,root);}
 static void RaisePermission(void* raw,void* self,const char* type,const char* text){auto& e=*static_cast<NativeEngine*>(raw);if(!self||!e.permission_raise)std::abort();e.permission_raise(self,type,text);}
 native_schema_guard_verifier::Bindings SchemaBindings(){
  native_schema_guard_verifier::Bindings b;b.vm=vm;b.metadata=&metadata;b.methods=&model;b.abstract_instances=&abstract_instances;b.owner=this;
  b.roots={this,FieldScope,FieldOracle,FieldRoot,FieldElement};b.decode_native_field=DecodeNativeGraphField;b.decode_global=DecodeGraph;b.decode_method=DecodeGraphMethod;b.decode_field=DecodeGraphField;b.stage_field=StageGraphField;b.method_class=GraphMethodClass;b.same_method=GraphSame;b.readable=GraphReadable;b.writable=GraphWritable;
  if(permissions_proven){b.final_fields=permission_service.get();b.access_permissions=permission_service.get();}return b;
 }
 bool PreparePermissionOwner(JNIEnv* env){
  schema_root_env=env;if(permissions_proven)return true;
  if(!permission_decoder_bound){
   if(!permission_decoder.Bind(hooks,{this,vm,PermissionThread,PermissionOracle,PermissionNativeScope},&permission_diagnostic))return false;
   permission_decoder_bound=true;
  }
  if(!permission_decoder.Ready()&&!permission_decoder.Calibrate(env,anchor,field_handles[0].strong_global,&permission_diagnostic))return false;
  if(!permission_service){
   permission_raise=reinterpret_cast<ThrowException>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"));
   if(!permission_raise||!exclusive_owned_roots::Service::Bind(hooks,&permission_exclusive,&permission_diagnostic))return false;
   final_field_service::Bindings b;b.native=SchemaBindings();b.native.decode_global=PermissionGlobal;b.registry=&retired_fields;b.decode_weak=PermissionWeak;b.raise=RaisePermission;
   b.register_strong=RegisterPermissionStrong;b.forget_strong=ForgetPermissionRoot;b.register_weak=RegisterPermissionWeak;b.forget_weak=ForgetPermissionWeak;
   permission_service=std::make_unique<final_field_service::Service>(b);
  }
  permissions_proven=final_field_calibration::Run(env,{this,permission_service.get(),&hooks,runtime.instrumentation,PermissionNativeScope,PermissionExclusiveScope},&permission_diagnostic);
  return permissions_proven;
 }
 static void* GraphReference(void* raw,void* array,size_t offset){return static_cast<NativeEngine*>(raw)->metadata.Reference(array,offset);}
 bool RootedSchemaPlan(source_commit::Plan* plan,std::string* error){
  return rooted_schema_plan::Read(schema_roots,schema_collected,{this,DecodeGraph,GraphReadable,GraphReference},plan,error);
 }
 bool PrepareSchemaPolicy(const source_commit::Plan& plan,std::string* error){
  schema_policies.clear();for(size_t i=0;i<plan.old_classes.size();++i){evolution::ClassSchema before,after;native_schema_policy::Plan policy;
   if(!native_schema_policy::ReadNativeDeclarations(metadata,model,plan.old_classes[i],&before,error)||!native_schema_policy::ReadNativeDeclarations(metadata,model,plan.new_classes[i],&after,error)||!native_schema_policy::Plan::Prepare(before,after,&policy,error))return false;
   schema_policies.push_back(std::move(policy));
  }
  std::vector<native_schema_policy::GuardRequest> requests;
  for(size_t i=0;i<schema_policies.size();++i){field_copy::CopyClassView identity;
   if(!metadata.ClassIdentity(plan.old_classes[i],&identity,error))return false;
   for(const auto& requirement:schema_policies[i].Requirements())requests.push_back({i,identity.loader,requirement});
  }
  return schema_verifier.PrepareGuardProofs(plan,requests,error);
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
 native_member_provider::Dependencies MemberDependencies() {
  native_member_provider::Dependencies d;d.hooks=&hooks;d.methods=methods.get();d.method_model=&model;
  d.metadata=&metadata;d.fields=&storage;d.cache=&cache;d.runtime=runtime;d.reflection=&reflection;
  d.retirement={this,MethodRetired,FieldRetired,RetireMethod,RetireField};d.is_retired=Retired;
  d.allow_field_removal=handles_proven;d.handle_guards={this,MethodHandlesReady,FieldVarHandlesReady,FieldMethodHandlesReady};
  if(schema_operation){d.expected_removal=native_member_provider::Removal::kSchemaEvolution;d.schemas=&schema_policies;d.schema_guards=&schema_verifier;}
  return d;
 }
 static std::unique_ptr<source_commit::Services> PrepareCommit(const source_commit::Plan& p,void* raw,std::string* error){
  auto& s=*static_cast<NativeEngine*>(raw);auto d=s.MemberDependencies();
  if(s.schema_operation){Fail(error,"Schema members require the unique native cleanup facade");return nullptr;}
  return s.provider.Prepare(p,d,error);
 }
 static std::unique_ptr<source_commit::Services> FinalizeSchema(const void* holder,const source_commit::Plan** plan,void* raw)noexcept{
  auto& e=*static_cast<NativeEngine*>(raw);
  if(!e.schema_run||!plan||holder!=e.schema_roots.holder||!redefinition_gate::Owned(e.vm))return nullptr;
  e.schema_finalizing=true;
  if(!RefreshStoppedMaps(&e,nullptr))return nullptr;
  return e.schema_run->Finalize(holder,plan);
 }
 static bool BeginSchemaRun(void* driver,void* raw,std::string* error){
  auto& e=*static_cast<NativeEngine*>(raw);JavaVM* actual=nullptr;
  if(e.schema_run||!e.source_env||!e.source_ti||!e.graph_current||
     e.source_env->GetJavaVM(&actual)!=JNI_OK||actual!=e.vm||e.source_env->ExceptionCheck())
   return Fail(error,"Schema Run lacks its original JNI/VM owner");
  const runnable_jni::Context context{e.graph_current(),e.source_env,e.vm};
  e.schema_run=native_schema_run_state::RunState::Begin(e.schema_dependencies,driver,context,e.source_ti,e.schema_inputs,e.CacheCalibration(),error);
  if(!e.schema_run&&error)e.source_factory_diagnostic=*error;return e.schema_run!=nullptr;
 }
 static void EndSchemaRun(void* driver,bool success,void* raw) noexcept {
  auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_run)return;
  if(!e.schema_run->End(driver,success))std::abort();
  // The verifier borrows this Run's actual mapping context; clear it before
  // destroying the context owner. All remaining JNI cleanup occurs Runnable.
  e.schema_verifier.Clear(e.source_env);e.schema_run.reset();
 }
 static bool BeforeSchemaVerification(void* owner,const transaction_root::Iterator* it,void* raw,std::string* error){
  auto& e=*static_cast<NativeEngine*>(raw);return e.schema_run&&e.schema_run->BeforeVerification(owner,it,error);
 }
 static void AfterSchemaVerification(void* owner,const transaction_root::Iterator* it,bool success,void* raw) noexcept {
  auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_run)std::abort();e.schema_run->AfterVerification(owner,it,success);
 }
 static void RefundSchema(void* raw) noexcept {auto& e=*static_cast<NativeEngine*>(raw);if(e.schema_run)e.schema_run->Refund();}
 static void CommitSchema(const source_commit::Plan& plan,void* raw) noexcept {
  auto& e=*static_cast<NativeEngine*>(raw);if(!e.schema_run)std::abort();e.schema_run->Commit(plan);
 }
 bool PrepareSchemaDependencies(JNIEnv* env,std::string* error){
  if(!source_stop.Calibrated())return Fail(error,SourceStopDiagnostic().c_str());
  if(!frame_geometry_proven)return Fail(error,frame_geometry_diagnostic.empty()?"Independent frame geometry not calibrated":frame_geometry_diagnostic.c_str());
  native_schema_run_state::Dependencies d;
  if(!source_capabilities.BindDependencies(&d,error)||!PrepareStoppedMaps(error))return false;
  d.hooks=&hooks;d.loading=&source_observers.Loading();d.stop=&source_stop;d.frames=&whole_frames;
  d.provider=&provider;d.registry=&schema_registry;d.guards=&schema_verifier;d.members=MemberDependencies();
  d.mapping={this,RefreshStoppedMaps,GraphExtent};
  if(!native_required_cleanup::Binding::Bind(hooks,env,d.members,&cleanup_binding,error))return false;
  d.cleanup=&cleanup_binding;schema_dependencies=d;return true;
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
 bool applying=false;size_t ordinary_source_step=0;std::string ordinary_source_diagnostic;
 bool PrimeOrdinarySource(JNIEnv* env,jvmtiEnv* ti,std::string* error){
  if(!ordinary_source_diagnostic.empty())return Fail(error,ordinary_source_diagnostic.c_str());
  if(source_capabilities.Ready()&&source_stop.Calibrated())return true;
  if(current||schema_run||graph_staging)return Fail(error,"Private ordinary source priming requires an unstaged outer operation");
  jclass type=env->GetObjectClass(input);struct Local{JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}} type_root{env,type};
  auto class_id=type?env->GetFieldID(type,"ordinarySourceClass","Ljava/lang/Class;"):nullptr;
  auto dex_id=type?env->GetFieldID(type,"ordinarySourceDex","[[B"):nullptr;
  auto klass=class_id?static_cast<jclass>(env->GetObjectField(input,class_id)):nullptr;Local class_root{env,klass};
  auto arrays=dex_id?static_cast<jobjectArray>(env->GetObjectField(input,dex_id)):nullptr;Local arrays_root{env,arrays};
  if(!klass||!arrays||env->ExceptionCheck()||env->GetArrayLength(arrays)!=3)return Fail(error,"Private ordinary source input unavailable");
  std::array<std::vector<unsigned char>,3> files;
  for(size_t i=0;i<files.size();++i){auto array=static_cast<jbyteArray>(env->GetObjectArrayElement(arrays,static_cast<jsize>(i)));Local bytes_root{env,array};
   if(!array||env->ExceptionCheck())return Fail(error,"Private ordinary source immutable DEX unavailable");
   const jsize size=env->GetArrayLength(array);if(size<=0)return Fail(error,"Private ordinary source immutable DEX is empty");
   files[i].resize(static_cast<size_t>(size));env->GetByteArrayRegion(array,0,size,reinterpret_cast<jbyte*>(files[i].data()));
   if(env->ExceptionCheck())return Fail(error,"Private ordinary source immutable DEX copy failed");
  }
  if(!ordinary_source_step){
   evolution::ClassSchema live;if(live_schema::ReadLiveSchema(ti,env,klass,&live,error)!=JVMTI_ERROR_NONE||!ordinary_source_fixture::Validate(live,files,error))return false;
   // Provenance belongs exclusively to the public isolated class/immutable
   // installed fixture just checked above, never a user candidate definition.
   if(!provenance.Store(env,klass,files[0]))return Fail(error,"Private ordinary source provenance could not be retained");
   ordinary_source_step=1;
  }
  while(ordinary_source_step<files.size()&&!(source_capabilities.Ready()&&source_stop.Calibrated())){
   const std::vector<Definition> definition{{klass,files[ordinary_source_step]}};
   const int result=ExecuteOperation(env,ti,definition,false,error);
   if(result!=JVMTI_ERROR_NONE||env->ExceptionCheck()){
    if(error)*error="private-ordinary-source round="+std::to_string(ordinary_source_step)+" result="+std::to_string(result)+": "+*error;
    return false;
   }
   const jint completed=static_cast<jint>(ordinary_source_step);++ordinary_source_step;
   jclass bootstrap=env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap");Local bootstrap_root{env,bootstrap};
   auto verify=bootstrap?env->GetStaticMethodID(bootstrap,"verifyOrdinarySource","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;I)Z"):nullptr;
   if(!verify||env->ExceptionCheck()||!env->CallStaticBooleanMethod(bootstrap,verify,input,completed)||env->ExceptionCheck())
    {ordinary_source_diagnostic="Private ordinary source original/fresh instance and retained compute oracle rejected";
     if(env->ExceptionCheck())env->ExceptionClear();
     return Fail(error,ordinary_source_diagnostic.c_str());}
  }
  if(!source_capabilities.Ready())return Fail(error,SourceFactoryDiagnostic().c_str());
  if(!source_stop.Calibrated())return Fail(error,SourceStopDiagnostic().c_str());
  PrepareCompiledFrames(env,ti);
  // The private worker must finish before clearing only its new exception.
  // Its refusal is reported separately; it never changes the ordinary Run.
  if(env->ExceptionCheck()){env->ExceptionClear();frame_geometry_diagnostic="Private compiled fixture calibration raised a Java exception";}
  return true;
 }
 int Apply(JNIEnv* env,jvmtiEnv* ti,const std::vector<Definition>& defs,std::string* error)override {
  if(!activated||current||applying||defs.empty())return owned_failure_diagnostic::Unavailable(error,"engine activation/reentrant input");
  applying=true;struct Outer{bool& active;~Outer(){active=false;}} outer{applying};
  bool requested_schema=false;
  for(const auto& definition:defs){evolution::ClassSchema before,after;std::string narrow_error;
   if(live_schema::ReadLiveSchema(ti,env,definition.klass,&before,error)!=JVMTI_ERROR_NONE||!evolution::ReadDexDeclarations(definition.dex,&after,error))return JVMTI_ERROR_INVALID_CLASS_FORMAT;
   if(!native_owned_removal::ValidateRemoval(before,after,handles_proven,&narrow_error,false)||(before.methods.size()==after.methods.size()&&before.fields.size()==after.fields.size()))requested_schema=true;
  }
  if(requested_schema&&!PrimeOrdinarySource(env,ti,error)){
   if(error)source_factory_diagnostic=*error;
   schema_operation=false;
   return owned_failure_diagnostic::Unavailable(error,"Automatic private ordinary source lifecycle calibration");
  }
  return ExecuteOperation(env,ti,defs,requested_schema,error);
 }
 int ExecuteOperation(JNIEnv* env,jvmtiEnv* ti,const std::vector<Definition>& defs,bool schema,std::string* error){
  schema_operation=schema;
  const bool batch=defs.size()>1||schema_operation;
  std::unique_ptr<redefinition_gate::Lease> lease;
  if(batch){
   if(!AtomicBatchReady()){if(error)*error=redefinition_gate::Ready(vm)?"Named rejectable post-verification graph window unavailable":redefinition_gate::Diagnostic();return JVMTI_ERROR_NOT_AVAILABLE;}
   lease=std::make_unique<redefinition_gate::Lease>(vm);
   if(!lease->Acquired())return Fail(error,"Another normal JVMTI transaction is active"),JVMTI_ERROR_NOT_AVAILABLE;
  }
  ClearFieldProofs(env);
  if(schema_operation&&!abstract_instances.Ready()&&!abstract_instances.Calibrate(hooks,env,metadata,model,error))return owned_failure_diagnostic::Unavailable(error,"abstract-instance calibration");
  struct FieldProofs {NativeEngine& engine;JNIEnv* env;~FieldProofs(){engine.ClearFieldProofs(env);}} field_scope{*this,env};
  if(batch?!StageGraph(env,ti,defs,error):!Preflight(env,ti,defs[0].klass,error)){
   if(error)*error=(batch?"graph/member staging: ":"single-Class member preflight: ")+*error;
   return owned_failure_diagnostic::Unavailable(error,batch?"candidate graph/member staging":"single-Class member preflight");
  }
  if(!cache.Prepare(error))return owned_failure_diagnostic::Unavailable(error,"interpreter-cache preparation");
  if(batch&&!PrepareSourceCacheWitness(env,error))return owned_failure_diagnostic::Unavailable(error,"source cache public witness preparation");
  // Observe a bounded ordinary unchanged-hierarchy removal first. Schema graph
  // preparation cannot stand in for the original source GC/final-stop proof.
  // Capability gaps leave ordinary behavior intact and cannot authorize Session.
  std::optional<native_source_calibration::Bridge> source_observation;
  source_env=env;source_ti=ti;source_fixture=env->NewGlobalRef(defs.front().klass);
  struct SourceContext {NativeEngine& e;JNIEnv* env;~SourceContext(){
   if(e.schema_run){e.schema_verifier.Clear(env);e.schema_run.reset();}
   if(e.source_fixture)env->DeleteGlobalRef(e.source_fixture);e.source_fixture=nullptr;e.source_env=nullptr;e.source_ti=nullptr;
  }} source_context{*this,env};
  if(!source_fixture||env->ExceptionCheck())return owned_failure_diagnostic::Unavailable(error,"Source original fixture rooting");
  if(!batch){
   source_factory_diagnostic.clear();
   thread_suspend_service::Service state;native_source_stop::Service candidate;
   source_stop={};source_stop_diagnostic.clear();
   const bool state_proven=thread_state_calibration::Calibrate(hooks,env,anchor,&state,&source_stop_diagnostic);
   if(state_proven){
    std::string native_error;
    if(native_scope.Prepared()||native_scope.Prepare(hooks,env,state,&native_error)){
     auto probe=[](const runnable_jni::Context& context,void* raw){
      return static_cast<native_scope_executor::Executor*>(raw)->Calibrate(context)==native_scope_executor::Result::Success;
     };
     // The actual ordinary JNI entry/return proves only a local Native scope.
     // No stop or GC reservation is retained across this Java/native call.
     runnable_executor::With(env,anchor,probe,&native_scope);
    }
    if(env->ExceptionCheck())return owned_failure_diagnostic::Unavailable(error,"Native-scope calibration raised a Java exception");
   }
   if(state_proven&&native_source_stop::Install(hooks,&source_stop_diagnostic)&&
      native_source_stop::Service::Bind(hooks,runtime.receivers.heap,state,&candidate,&source_stop_diagnostic)){
    // Optional canonical bridge is calibrated later at actual ordinary Run
    // entry; JNI bootstrap remains Native and grants no continuous reservation.
    if(candidate.RequiresOriginalWaiting())
     native_source_stop::InstallOriginalWaitingScope(hooks,&source_stop_diagnostic);
    auto current_thread=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    void* self=current_thread?current_thread():nullptr;size_t extent=0;
    if(self&&method_identity::ReadableMappingExtent(self,&extent)){
     if(source_observers.Install(hooks,env,runtime,state,self,extent,&source_stop_diagnostic)){
      const runnable_jni::Context context{self,env,vm};
      const bool capture=source_capabilities.Prepare(hooks,storage,metadata,runtime,context,&source_factory_diagnostic)&&
       source_capabilities.InstallHooks(native_owned_removal::ObserveSourceFailure,&source_factory_diagnostic);
      if(!frame_geometry_proven)PrepareFrameGeometry(state,self,&frame_geometry_diagnostic);
      const native_source_calibration::RunLifecycle lifecycle=capture?native_source_calibration::RunLifecycle{this,CalibrationBegin,CalibrationEnd}:native_source_calibration::RunLifecycle{};
      source_observation.emplace(candidate,self,extent,source_observers.Loading(),context,lifecycle);
     }
    }
    else source_stop_diagnostic="Ordinary source calibration current Thread mapping unavailable";
   }
  }
  if(schema_operation){source_factory_diagnostic.clear();if(!PrepareSchemaDependencies(env,error)){
   if(error)source_factory_diagnostic=*error;return owned_failure_diagnostic::Unavailable(error,"Full schema owner dependency preparation");
  }}
  current=this;struct Clear {~Clear(){NativeEngine::current=nullptr;}} clear;
  native_owned_removal::Provider source_provider{Ready,PrepareCommit,&metadata,this,handles_proven,batch?StagedInputs:nullptr,batch?CheckGraph:nullptr,schema_operation,schema_operation?FinalizeSchema:nullptr};
  if(schema_operation){source_provider.begin_run=BeginSchemaRun;source_provider.end_run=EndSchemaRun;
   source_provider.before_verification=BeforeSchemaVerification;source_provider.after_verification=AfterSchemaVerification;
   source_provider.refund=RefundSchema;source_provider.commit=CommitSchema;}
  int result=native_owned_removal::RunBatch(hooks,env,ti,defs,source_provider,error);
  if(source_observation){
   if(source_observation->Authorized())source_stop=source_observation->Binding();
   else source_stop_diagnostic=source_observation->Failure();
   source_observation.reset();
   if(result==JVMTI_ERROR_NONE&&!env->ExceptionCheck()){
    PrepareCompiledFrames(env,ti);
    // A private post-Run calibration cannot change a committed original result.
    // Preserve its refusal as capability diagnostics, release its worker first,
    // then clear only the newly raised private-fixture exception.
    if(env->ExceptionCheck()){env->ExceptionClear();frame_geometry_diagnostic="Private compiled fixture calibration raised a Java exception";}
   }
  }
  if(schema_operation&&result!=JVMTI_ERROR_NONE&&error)source_factory_diagnostic=*error;
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
 e.frame_methods.clear();e.frame_methods.reserve(r.method_oracles->size());
 for(size_t n=0;n<r.method_oracles->size();++n){const auto& oracle=(*r.method_oracles)[n];
  const abi::arm64::android_common::RunnableJniScope actual{scope.self,scope.env,scope.vm};
  void* method=r.leaf(actual,oracle.strong_global_method);
  if(!method)return false;e.frame_methods.push_back({method,e.model.readable_extents[n],oracle.dex_method_index,oracle.java_modifiers});
 }
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
bool NativeEngine::ResolveCacheWitness(void* raw,const runnable_jni::Context& scope,
 source_handle_roots::Pool& pool,void* cache_root,void* class_root,std::string*error){
 auto& e=*static_cast<NativeEngine*>(raw);void* klass=nullptr;
 if(scope.env->ExceptionCheck()||scope.vm!=e.vm||scope.self!=e.graph_current()||
    !redefinition_gate::Owned(e.vm)||!pool.Read(class_root,&klass))
  return Fail(error,"Cache resolution actual source/public root owner unavailable");
 field_copy::CopyClassView identity;
 if(!e.metadata.ClassIdentity(klass,&identity,error))return false;
 auto* loader=pool.Add(identity.loader,error);if(!loader)return false;
 class Proof final:public source_cache_resolution::Roots {
  public:
   Proof(NativeEngine&engine,const runnable_jni::Context&scope,source_handle_roots::Pool&pool):e(engine),c(scope),p(pool){}
   bool Snapshot(const source_cache_resolution::Witness&w,void**out,std::string*error)const override{
    if(c.env->ExceptionCheck())return Fail(error,"Original cache resolution raised JNI exception");
    void* cache=nullptr,*klass=nullptr,*loader=nullptr;
    if(c.vm!=e.vm||c.self!=e.graph_current()||!redefinition_gate::Owned(e.vm)||
       !p.Read(w.cache_slot,&cache)||!p.Read(w.class_slot,&klass)||!p.Read(w.loader_slot,&loader)||!cache||!klass)
     return Fail(error,"Cache resolution live Class/cache/loader root provenance rejected");
    field_copy::CopyClassView identity;
    if(!e.metadata.ClassIdentity(klass,&identity,error))return false;
    if(identity.loader!=loader||e.metadata.Reference(klass,offsetof(class_metadata::Class11,dex_cache))!=cache)
     return Fail(error,"Cache resolution exact public Class cache/loader namespace changed");
    *out=klass;return true;
   }
   std::string DescribeField(const source_cache_resolution::Witness&w,void*field)const override{
    void* klass=nullptr;if(!field||!p.Read(w.class_slot,&klass))return " member=null-or-root-unavailable";
    for(bool is_static:{false,true}){
     field_array::View fields;std::string ignored;uint32_t position=0;
     if(!e.metadata.Fields(klass,is_static,&fields,&ignored)||!fields.IndexOf(field,&position))continue;
     // Only the independently bounded native array member is read as ArtField.
     field_copy::CopyFieldView text;
     if(!e.metadata.FieldIdentity(field,&text,&ignored))return " member=owned-array-text-unavailable";
     uint32_t index=0;std::memcpy(&index,static_cast<const unsigned char*>(field)+offsetof(field_array::Field,dex_field_index),4);
     return " member=owned-"+std::string(is_static?"static":"instance")+" nativeIdx="+std::to_string(index)+
      " name="+std::string(text.name?text.name:"?")+" desc="+std::string(text.descriptor?text.descriptor:"?");
    }
    return " member=outside-public-Class-native-arrays";
   }
  private:NativeEngine&e;const runnable_jni::Context&c;source_handle_roots::Pool&p;
 } proof(e,scope,pool);
 const auto&w=e.source_cache_witness;
 source_cache_resolution::Witness witness{cache_root,class_root,loader,w.ids.type_index,w.field_index,w.method_index,w.native_field,w.native_method};
 return e.cache_resolver.Resolve(witness,proof,error);
}
struct SourceCacheWitnessRequest {
 NativeEngine* engine;std::string* error;
 abi::arm64::android_common::FromReflectedMethod method_leaf;
};
bool CaptureSourceCacheWitness(const runnable_jni::Context& scope,void* raw){
 auto& request=*static_cast<SourceCacheWitnessRequest*>(raw);auto& e=*request.engine;
 auto& witness=e.source_cache_witness;void* klass=nullptr;void* mirror=nullptr;
 if(scope.vm!=e.vm||scope.self!=e.graph_current()||!redefinition_gate::Owned(e.vm)||
    !runnable_executor::DecodeStrongGlobal(scope,witness.klass,&klass)||
    !runnable_executor::DecodeStrongGlobal(scope,witness.field,&mirror))
  return Fail(request.error,"Public cache witness Runnable owner/class/field root identity rejected");
 const auto layout=e.model.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 size_t extent=0;void* method=nullptr;
 // The executor's carrier is a GlobalRef (or immutable boot Method), not read().
 // Read the rooted fixture Method through ART's named leaf in this real scope.
 if(!source_cache_method_witness::Read(scope,request.method_leaf,witness.method,
       layout,witness.method_index,method_identity::ReadableMappingExtent,&method,request.error))return false;
 auto* field=e.graph_field_leaf?e.graph_field_leaf(mirror):nullptr;field_copy::CopyFieldView text;
 if(!field)return Fail(request.error,"Public cache Field named GetArtField returned null");
 if(!method_identity::ReadableMappingExtent(field,&extent)||extent<sizeof(field_array::Field))
  return Fail(request.error,"Public cache Field native source prefix unreadable");
 void* declaring=e.metadata.FieldDeclaringClass(field);
 if(declaring!=klass)return Fail(request.error,"Public cache Field native declaring Class differs from independent public Class");
 if(!e.metadata.FieldIdentity(field,&text,request.error)){
  if(request.error)*request.error="Public cache Field native text unavailable: "+*request.error;
  return false;
 }
 uint32_t index=0;std::memcpy(&index,static_cast<unsigned char*>(field)+offsetof(field_array::Field,dex_field_index),sizeof(index));
 if(!source_cache_field_witness::Check(klass,declaring,text.name,text.descriptor,index,
     witness.field_index,field_array::Flags(field),witness.public_field_modifiers,request.error))return false;
 // No safepoint between this rooted Class read and the original AddGlobalRef.
 // The real global is published before ART's allocation-tracking callback.
 void* cache=e.metadata.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
 if(!cache||!method_identity::ReadableMappingExtent(cache,&extent)||extent<112)
  return Fail(request.error,"Public fixture DexCache extent unavailable");
 auto root=NativeEngine::RootManaged(&e,cache);if(!root)return Fail(request.error,"Public fixture DexCache strong root creation rejected");
 witness.cache=root;witness.native_field=field;witness.native_method=method;
 return true;
}
bool NativeEngine::PrepareSourceCacheWitness(JNIEnv* env,std::string* error){
 if(!env||!input||!redefinition_gate::Owned(vm))return Fail(error,"Source cache witness lacks ordinary owning transaction");
 if(env->ExceptionCheck())return Fail(error,"Public cache witness entered with pending JNI exception");
 if(!source_cache_resolution::Service::Bind(hooks,runtime.receivers.class_linker,&cache_resolver,error))return false;
 jclass type=env->GetObjectClass(input);
 struct Local {JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}} type_scope{env,type};
 if(!type||env->ExceptionCheck())return Fail(error,"Public cache witness Inputs class unavailable");
 const char* descriptor="Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;";
 if(!source_cache_witness.klass){
  PublicCacheWitness candidate;Roots owned{env,{}};
  auto own=[&](const char* name,const char* signature){
   if(env->ExceptionCheck())return static_cast<jobject>(nullptr);
   auto id=env->GetFieldID(type,name,signature);
   if(!id||env->ExceptionCheck()){if(error)*error=std::string("Public cache witness Inputs field unavailable: ")+name;return static_cast<jobject>(nullptr);}
   auto value=env->GetObjectField(input,id);Local value_scope{env,value};
   if(!value||env->ExceptionCheck()){if(error)*error=std::string("Public cache witness Inputs value unavailable: ")+name;return static_cast<jobject>(nullptr);}
   auto global=owned.Own(value);
   if(!global||env->ExceptionCheck()){if(error)*error=std::string("Public cache witness strong root unavailable: ")+name;return static_cast<jobject>(nullptr);}
   return global;
  };
  candidate.klass=own("cacheClass","Ljava/lang/Class;");if(!candidate.klass)return false;
  candidate.field=own("cacheField","Ljava/lang/reflect/Field;");if(!candidate.field)return false;
  candidate.method=own("cacheRead","Ljava/lang/reflect/Method;");if(!candidate.method)return false;
  auto id=env->GetFieldID(type,"cacheDex","[B");
  if(!id||env->ExceptionCheck())return Fail(error,"Public cache witness Inputs.cacheDex field unavailable");
  auto bytes=static_cast<jbyteArray>(env->GetObjectField(input,id));Local bytes_scope{env,bytes};
  if(!bytes||env->ExceptionCheck())return Fail(error,"Public cache witness immutable DEX bytes unavailable");
  jsize length=env->GetArrayLength(bytes);
  if(length<=0||env->ExceptionCheck())return Fail(error,"Public cache witness immutable DEX length unavailable");
  std::vector<unsigned char> original(static_cast<size_t>(length));
  env->GetByteArrayRegion(bytes,0,length,reinterpret_cast<jbyte*>(original.data()));
  if(env->ExceptionCheck())return Fail(error,"Public cache witness immutable DEX copy rejected");
  evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;
  if(!SelectInstalledClass({original},descriptor,&schema,&counts,error,&candidate.ids))return false;
  for(const auto& f:schema.fields)if(f.name=="number"&&f.descriptor=="I"&&f.access==(1u|0x40u))candidate.field_index=f.dex_index;
  for(const auto& m:schema.methods)if(m.name=="read"&&m.descriptor=="()I"&&m.access==1u)candidate.method_index=m.dex_index;
  if(candidate.field_index==UINT32_MAX||candidate.method_index==UINT32_MAX)
   return Fail(error,"Immutable cache fixture member declarations unavailable");
  roots.insert(roots.end(),owned.values.begin(),owned.values.end());owned.values.clear();
  source_cache_witness=candidate;
 }
 if(!source_cache_witness.klass||!source_cache_witness.field||!source_cache_witness.method)
  return Fail(error,"Public cache witness retained roots incomplete");
 jclass bootstrap=env->FindClass("com/github/ytlog/artdcevm/internal/ProductionBootstrap");Local bootstrap_scope{env,bootstrap};
 if(!bootstrap||env->ExceptionCheck())return Fail(error,"Public cache witness bootstrap class unavailable");
 auto warm=env->GetStaticMethodID(bootstrap,"warmSourceCache","(Lcom/github/ytlog/artdcevm/internal/ProductionBootstrap$Inputs;)Ljava/lang/Class;");
 if(!warm||env->ExceptionCheck())return Fail(error,"Public cache witness original warm method unavailable");
 auto expected=env->CallStaticObjectMethod(bootstrap,warm,input);Local expected_scope{env,expected};
 if(!expected||env->ExceptionCheck())return Fail(error,"Original DEX cache warm invocation rejected");
 const bool same=env->IsSameObject(expected,source_cache_witness.klass);
 if(!same||env->ExceptionCheck())return Fail(error,"Original DEX cache warm class identity changed");
 // Independent ordinary-JNI public Field oracle, completed before Runnable.
 jclass field_type=env->GetObjectClass(source_cache_witness.field);Local field_type_scope{env,field_type};
 if(!field_type||env->ExceptionCheck())return Fail(error,"Public cache Field reflection class unavailable");
 auto declaring_id=env->GetMethodID(field_type,"getDeclaringClass","()Ljava/lang/Class;");
 if(!declaring_id||env->ExceptionCheck())return Fail(error,"Public cache Field declaring Class oracle unavailable");
 auto public_declaring=env->CallObjectMethod(source_cache_witness.field,declaring_id);Local declaring_scope{env,public_declaring};
 if(!public_declaring||env->ExceptionCheck())return Fail(error,"Public cache Field declaring Class oracle invocation rejected");
 const bool declaring_matches=env->IsSameObject(public_declaring,source_cache_witness.klass);
 if(!declaring_matches||env->ExceptionCheck())return Fail(error,"Public cache Field declaring Class oracle identity changed");
 auto modifiers_id=env->GetMethodID(field_type,"getModifiers","()I");
 if(!modifiers_id||env->ExceptionCheck())return Fail(error,"Public cache Field modifiers oracle unavailable");
 const jint modifiers=env->CallIntMethod(source_cache_witness.field,modifiers_id);
 if(env->ExceptionCheck())return Fail(error,"Public cache Field modifiers oracle invocation rejected");
 if(modifiers!=0x41)return Fail(error,"Public cache Field modifiers differ from immutable DEX");
 source_cache_witness.public_field_modifiers=static_cast<uint32_t>(modifiers);
 if(source_cache_witness.cache){env->DeleteGlobalRef(source_cache_witness.cache);source_cache_witness.cache=nullptr;}
 auto leaf=reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(
   hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod));
 if(!leaf)return Fail(error,"Public cache witness named FromReflectedMethod unavailable");
 SourceCacheWitnessRequest request{this,error,leaf};
 auto result=runnable_executor::With(env,source_cache_witness.method,CaptureSourceCacheWitness,&request);
 if(result!=runnable_jni::Result::kSuccess){
  if(error&&error->empty())*error="Public cache witness Runnable dispatch rejected: result="+std::to_string(static_cast<int>(result))+", route="+runnable_executor::Route();
  return false;
 }
 return source_cache_witness.cache!=nullptr||Fail(error,"Public cache witness completed without DexCache root");
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
 if(!e.metadata.VerifyProvenDeclaration(scope,r.owner,r.schema->descriptor.c_str(),r.fields.data(),r.fields.size(),&r.error)){
  r.error="Target proven field declaration count="+std::to_string(r.fields.size())+": "+r.error;return false;
 }
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
  if(graph_staging&&!StageGraphField(this,env,target,fields[i],(modifiers&8)!=0,error))return false;
  request.fields.push_back({root,names[i].c_str(),descriptors[i].c_str(),uint32_t(modifiers),member->second.dex_index});expected_fields.erase(member);
 }
 if(!request.decode_method||!request.method_leaf||!request.field_leaf||env->ExceptionCheck()||
    runnable_executor::With(env,owner,TargetObserve,&request)!=runnable_jni::Result::kSuccess){if(error)*error=request.error.empty()?"Target rooted native/public identity proof rejected":request.error;return false;}
 StagedClass stage{static_cast<jclass>(env->NewGlobalRef(target)),request.field_proof_base,{}, {}};
 if(!stage.owner||env->ExceptionCheck())return false;
 for(jint i=0;i<f;i++){stage.ids.push_back(fields[i]);auto mirror=env->NewGlobalRef(request.fields[i].strong_global_field);if(!mirror||env->ExceptionCheck()){for(auto m:stage.mirrors)env->DeleteGlobalRef(m);env->DeleteGlobalRef(stage.owner);return false;}stage.mirrors.push_back(mirror);}
 staged_classes.push_back(std::move(stage));return true;
}
bool NativeEngine::StageGraph(JNIEnv* env,jvmtiEnv* ti,const std::vector<Definition>& definitions,std::string* error){
 graph_staging=true;
 graph_current=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 graph_field_leaf=reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv"));
 graph_add_global=reinterpret_cast<decltype(graph_add_global)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kAddGlobalRef));
 if(!graph_current||!graph_field_leaf||!graph_add_global)return Fail(error,"Paused graph native readers unavailable");
 graph_decode=reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so",abi::arm64::android14::kDecodeGlobalJObject));
 if(!graph_decode)graph_decode=reinterpret_cast<DecodeObject>(hooks.FindSymbol("libart.so",abi::arm64::android11::kDecodeJObject));
 graph_decode_method=reinterpret_cast<decltype(graph_decode_method)>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"));
 graph_same=reinterpret_cast<decltype(graph_same)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
 if(!graph_decode||!graph_decode_method||!graph_same)return Fail(error,"Named source-owned graph readers unavailable");
 for(const auto& oracle:field_method_handles)if(!StageGraphField(this,env,bootstrap_field_owner,oracle.field,contextual_field_id::IsStatic(oracle.identity),error))return false;
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
 if(runnable_executor::With(env,anchor,ObserveGraphReaders,this)!=runnable_jni::Result::kSuccess)return Fail(error,graph_reader_error.empty()?"Native graph Runnable scope failed before reader proof":graph_reader_error.c_str());
 if(schema_operation){
  // These public original Class globals belong to this one staged operation.
  // Proposed declarations come from the actual incoming DEX, not replacement
  // metadata captured later by the source allocator.
  schema_inputs.reserve(definitions.size());
  for(const auto& definition:definitions){native_hierarchy_graph::Input input;
   for(const auto& stage:staged_classes)if(env->IsSameObject(stage.owner,definition.klass)){if(input.strong_original)return Fail(error,"Duplicate hierarchy input Class");input.strong_original=stage.owner;}
   if(!input.strong_original||env->ExceptionCheck()||
      live_schema::ReadLiveSchema(ti,env,input.strong_original,&input.before,error)!=JVMTI_ERROR_NONE||
      !evolution::ReadDexDeclarations(definition.dex,&input.after,error))return false;
   schema_inputs.push_back(std::move(input));
  }
  PreparePermissionOwner(env);if(env->ExceptionCheck())return Fail(error,"Final permission calibration left a pending exception");
  if(!schema_verifier.Stage(env,ti,graph_bases,SchemaBindings(),error))return false;
  std::vector<jclass> affected;affected.reserve(staged_classes.size());
  for(const auto& stage:staged_classes)affected.push_back(stage.owner);
  if(!schema_verifier.MatchesAffected(env,affected,error))return Fail(error,"Caller staging loaded an unstaged affected descendant; complete candidate preflight is required before source entry");
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
  if(window==native_owned_removal::GraphWindow::Verified&&e.permissions_proven){
   std::vector<jclass> old_classes,replacements;
   struct Locals{JNIEnv* env;std::vector<jclass>& a;std::vector<jclass>& b;~Locals(){for(auto root:a)env->DeleteLocalRef(root);for(auto root:b)env->DeleteLocalRef(root);}} release{env,old_classes,replacements};
   for(const auto& part:rooted.partitions)for(size_t n=0;n<part.class_count;++n){
    old_classes.push_back(static_cast<jclass>(env->GetObjectArrayElement(part.old_classes,static_cast<jsize>(n))));
    replacements.push_back(static_cast<jclass>(env->GetObjectArrayElement(part.new_classes,static_cast<jsize>(n))));
   }
   if(env->ExceptionCheck()||!e.permission_service->Stage(env,ti,old_classes,replacements,error))return false;
  }
  if(window==native_owned_removal::GraphWindow::Collected){
   if(!e.RefreshMaps(error)||!e.RootedSchemaPlan(&e.prepared_schema_plan,error))return false;
   if(e.permissions_proven){
    if(!e.permission_service->PreparePermissions(e.prepared_schema_plan,error))return false;
    // Public root publication may safepoint; refresh the actual Holder globals
    // before sealing against the permission owner's paired strong globals.
    if(!e.RootedSchemaPlan(&e.prepared_schema_plan,error)||!e.permission_service->Seal(e.prepared_schema_plan,error))return false;
   }
   if(!e.schema_run||!e.schema_verifier.Seal(e.prepared_schema_plan,error)||!e.PrepareSchemaPolicy(e.prepared_schema_plan,error))return false;
   if(!e.permissions_proven)for(const auto& policy:e.schema_policies)for(const auto& requirement:policy.Requirements())
    if(requirement.kind==native_schema_policy::GuardKind::FinalFields||requirement.kind==native_schema_policy::GuardKind::AccessPermissions){
     if(error)*error="Actual final/access permission calibration unavailable: "+e.permission_diagnostic;return false;
    }
   return e.schema_run->Collected(rooted,e.prepared_schema_plan,error);
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
 e.runtime.receivers=jit;e.anchor=owner_root;e.bootstrap_field_owner=static_cast<jclass>(storage_class);
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
