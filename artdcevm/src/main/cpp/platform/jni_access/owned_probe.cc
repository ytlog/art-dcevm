#include "platform/common/contextual_field_stage.h"
#if defined(ARTDCEVM_TESTING)
namespace artdcevm::jni_access {void ReleaseOwnedFieldProofStage();}
#endif
// Debug-only owned JNI calibration, independent of the full evolution backend.
#ifdef ARTDCEVM_TESTING
#include "platform/jni_access/capability_selection.h"
#include "platform/common/runtime_service_calibration.h"
#include "platform/common/native_cache_service.h"
#include "platform/common/modern_method_guards.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/modern_retirement_cleanup.h"
#include "platform/common/owned_method_table.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/runnable_global_ref_bridge.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/owned_context_calibration.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/android17/method_table.h"
#include "platform/common/executable_mirror_calibration.h"
#include "platform/common/native_method_binding_calibration.h"
#include "platform/common/modern_method_services.h"
#include "platform/common/owned_method_table.h"
#include "platform/jni_access/owned_evolution_proofs.h"
#include "platform/common/owned_jit_receivers.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include "platform/abi/arm64/android15/jni_current_thread.h"
#include "platform/abi/common/suspend_all.h"
#include "hooks/shadow_hook_engine.h"
#include "support/elf_modules.h"
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace artdcevm::jni_access {
EvolutionProofs& OwnedEvolutionProofs(){static thread_local EvolutionProofs proofs;return proofs;}
void CaptureFieldProofs(const modern_field_metadata::FieldMetadata& metadata,const modern_field_storage::Service& fields){auto& p=OwnedEvolutionProofs();p.metadata=metadata;p.fields=fields;}
namespace {

owned_context_calibration::Service context_service;
RuntimeCandidate profile{};

bool initialized=false;
thread_local const char* last_route="uncalibrated";
thread_local int last_table_count=-1;
thread_local size_t last_cache_threads=0;
thread_local method_identity::Calibration last_method_model;
thread_local runtime_services::Snapshot runtime_services_observed;
std::mutex initialization;
abi::arm64::android11::CurrentThread current=nullptr;
abi::arm64::android11::CurrentPeer const_peer=nullptr;
abi::arm64::android15::CurrentPeer mutable_peer=nullptr;
abi::arm64::android11::DecodeJObject legacy_decode=nullptr;
runnable_jni::GlobalDecoder global_decode;
struct Probe {JNIEnv* env;JavaVM* vm;void* self;jobject carrier;jobject thread_global;int hits=0;bool use_global=false;};
bool Observe(const runnable_jni::Context& context,void* data) {
  auto& probe=*static_cast<Probe*>(data);
  if(context.env!=probe.env||context.vm!=probe.vm||context.self!=probe.self||current()!=context.self)return false;
  if(context.env->GetObjectRefType(probe.thread_global)!=JNIGlobalRefType||context.env->ExceptionCheck())return false;
  void* object=nullptr;
  if(profile.decoder==DecoderModel::kLegacyAny)
    object=reinterpret_cast<void*>(legacy_decode(context.self,probe.thread_global).reference);
  else if(!global_decode.Decode(context,probe.thread_global,&object))return false;
  // No JNI safepoint or managed dereference between the independently obtained
  // borrowed identities. Never take this current-thread oracle to another Thread.
  void* peer=const_peer?const_peer(context.self):mutable_peer(context.self);
  if(!object||object!=peer)return false;
  auto nested=runnable_executor::With(context.env,probe.thread_global,Observe,data);
  if(nested!=runnable_jni::Result::kReentrant)return false;
  ++probe.hits;return true;
}
struct MethodProbe {
  Probe probe;
  abi::common::FromReflectedMethod leaf;
  const method_identity::Oracle* oracles;
  size_t count;
  method_identity::Calibration calibration;
  jobject class_global;
  evolution::MappedDexMemberCounts counts;
  bool table_candidate;
  const void* registered_binding;
};
void JNICALL OwnedNativeMarker(JNIEnv*, jobject) {}
bool UnretiredOwnedFixture(void*) { return false; }
bool ObserveMethods(const runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<MethodProbe*>(data);
  if(!Observe(context,&request.probe))return false;
  std::string error;
  if(!method_identity::Calibrate(context,request.leaf,request.oracles,request.count,
                                    &request.calibration,&error))return false;
  last_method_model=request.calibration;
  void* klass=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,request.class_global,&klass))return false;
  method_array::View table;
  if(!owned_method_table::Calibrate(context,request.leaf,request.calibration,klass,
      request.counts,request.oracles,request.count,&table,&error))return false;
  if(!executable_mirror::Calibrate(context,request.leaf,
      runnable_executor::DecodeStrongGlobal,request.oracles,request.count,&error))return false;
  bool native_checked=false;
  for(size_t i=0;i<request.count;++i)if(request.oracles[i].java_modifiers&0x100u) {
    if(!native_method_binding::Calibrate(context,request.leaf,request.oracles[i],
        request.registered_binding,request.calibration.model,&error))return false;
    native_checked=true;
  }
  if(!native_checked)return false;
  if(runtime_services_observed.receivers.class_linker) {
    ShadowHookEngine hooks;
    size_t native_index=0;
    while(native_index<request.count&&!(request.oracles[native_index].java_modifiers&0x100u))++native_index;
    auto binding=modern_methods::Service::BindOwned(hooks,context,request.leaf,
        request.oracles[native_index],request.registered_binding,
        runtime_services_observed.receivers.class_linker,request.calibration.model,&error);
    auto mirrors=executable_mirror::Layout::Probe(context,request.leaf,
        runnable_executor::DecodeStrongGlobal,request.oracles,request.count,&error);
    if(!binding||!mirrors)return false;
    auto service=modern_methods::Service::PrepareTables(*binding,klass,table,
        klass,table,UnretiredOwnedFixture,&error);
    if(!service||service->Count(klass)!=table.count)return false;
    for(size_t i=0;i<table.count;++i) {
      void* method=service->At(klass,i);
      if(service->DeclaringClass(method)!=klass||!service->SameIdentity(method,method)||
          service->IsRetired(method)||service->Flags(method)!=method_metadata::ReadAccessFlags(method))return false;
    }
    // Named name/signature comparison must distinguish two independently
    // declared methods, including the constructor.
    if(table.count>1&&service->SameIdentity(service->At(klass,0),service->At(klass,1)))return false;
    auto& proofs=OwnedEvolutionProofs();proofs.methods=std::move(binding);
    proofs.mirrors=std::move(mirrors);proofs.method_model=request.calibration;
  }
  last_table_count=static_cast<int>(table.boundaries.declared_count);
  return true;
}
int Run(JNIEnv* env,jobject carrier,jobject thread_global) {
  last_route="uncalibrated";
  if(!env||!carrier||!thread_global||env->ExceptionCheck())return -1;
  JavaVM* vm=nullptr;if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -2;
  std::unique_lock<std::mutex> guard(initialization);
  if(!initialized) {
    RuntimeCandidate candidate{};std::string error;
    static ShadowHookEngine hooks;if(!hooks.Initialize(&error))return -4;
    auto resolve=[](void* context,const char* name)->void* {
      return static_cast<ShadowHookEngine*>(context)->FindSymbol("libart.so",name);
    };
    if(!SelectRuntimeCandidate(resolve,&hooks,&candidate,&error))return -3;
    auto get_current=reinterpret_cast<decltype(current)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
    const char* peer_name=candidate.peer==PeerModel::kConst?abi::arm64::android11::kCurrentPeer:abi::arm64::android15::kCurrentPeer;
    const char* decode_name=candidate.decoder==DecoderModel::kLegacyAny?abi::arm64::android11::kDecodeJObject:runnable_jni::GlobalDecoder::SymbolName();
    void* peer=hooks.FindSymbol("libart.so",peer_name);
    void* decode=hooks.FindSymbol("libart.so",decode_name);
    if(!get_current||!peer||!decode)return -5;
    if(!runnable_executor::Initialize(hooks,env,&error))return -6;
    current=get_current;profile=candidate;
    if(candidate.peer==PeerModel::kConst)const_peer=reinterpret_cast<decltype(const_peer)>(peer);
    else mutable_peer=reinterpret_cast<decltype(mutable_peer)>(peer);
    if(candidate.decoder==DecoderModel::kLegacyAny)legacy_decode=reinterpret_cast<decltype(legacy_decode)>(decode);
    else if(!runnable_jni::GlobalDecoder::Bind(runnable_jni::GlobalDecoder::Representation::kMatchedReleaseNoPoison,decode,&global_decode))std::abort();
    owned_context_calibration::Service::Bind(
        hooks.FindSymbol("libart.so",owned_context_calibration::Service::SymbolName()),&context_service);
    initialized=true;
    // Binding is a diagnostic candidate only. Observed release pointer equality
    // below does not mint any production calibration token or evolution profile.
  }
  guard.unlock();
  void* self=current();if(!self||env->GetObjectRefType(thread_global)!=JNIGlobalRefType)return -7;
  jmethodID expected=env->FromReflectedMethod(carrier);if(!expected||env->ExceptionCheck())return -8;
  Probe probe{env,vm,self,carrier,thread_global};
  for(int i=0;i<128;++i) {
    auto result=runnable_executor::With(env,thread_global,Observe,&probe);
    if(result!=runnable_jni::Result::kSuccess)return -100-static_cast<int>(result);
    if(current()!=self||env->FromReflectedMethod(carrier)!=expected||env->ExceptionCheck())return -10;
  }
  if(probe.hits!=128)return -11;
  last_route=runnable_executor::Route();
  return 128;
}
}
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probe(JNIEnv* env,jclass,jobject carrier,jobject thread) {
  // Own the JNI root on the calling native thread, before any scope calibration.
  if(!thread||env->ExceptionCheck())return -1;
  jobject global=env->NewGlobalRef(thread);if(!global)return -12;
  int result=artdcevm::jni_access::Run(env,carrier,global);
  env->DeleteGlobalRef(global);return result;
}
#endif
#if defined(ARTDCEVM_TESTING)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_methodTableCount(JNIEnv*,jclass) {
  return artdcevm::jni_access::last_table_count;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeJitServices(JNIEnv* env,jclass,jobject hot,jclass owner,jint index,jint flags,jclass target,jbyteArray dex) {
  using namespace artdcevm;
  if(!hot||!owner||!target||!dex||env->ExceptionCheck())return nullptr;
  struct Roots {JNIEnv* env;jobject method,klass;
    ~Roots(){if(method)env->DeleteGlobalRef(method);if(klass)env->DeleteGlobalRef(klass);}
  } roots{env,env->NewGlobalRef(hot),nullptr};
  if(!roots.method||env->ExceptionCheck())return nullptr;
  roots.klass=env->NewGlobalRef(owner);
  if(!roots.klass||env->ExceptionCheck()||env->GetArrayLength(dex)<=0)return nullptr;
  jobject method=roots.method,klass=roots.klass;
  std::vector<unsigned char> bytes(static_cast<size_t>(env->GetArrayLength(dex)));
  env->GetByteArrayRegion(dex,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));
  ShadowHookEngine hooks;std::string error;runtime_services::View view;
  method_identity::Oracle oracle{method,static_cast<uint32_t>(index),static_cast<uint32_t>(flags)};
  bool passed=owned_jit_receivers::CalibrateJitReceivers(hooks,env,klass,oracle,
      jni_access::last_method_model,jni_access::runtime_services_observed,target,bytes.data(),bytes.size(),&view,&error);
  if(passed){auto& proofs=jni_access::OwnedEvolutionProofs();proofs.observed=jni_access::runtime_services_observed;proofs.observed.receivers=view;}
  if(env->ExceptionCheck())return nullptr;
  return env->NewStringUTF(passed?"jit,code-cache,heap":error.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_artBuildId(JNIEnv* env,jclass) {
  auto module=artdcevm::FindLoadedModule("libart.so");
  return env->NewStringUTF(module.build_id.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeRoute(JNIEnv* env,jclass) {
  return env->NewStringUTF(artdcevm::jni_access::last_route);
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeContext(JNIEnv*,jclass) {
  artdcevm::owned_context_calibration::Service service;
  {
    std::lock_guard<std::mutex> lock(artdcevm::jni_access::initialization);
    service=artdcevm::jni_access::context_service;
  }
  artdcevm::owned_context_calibration::Report report;
  if(!service.Run(128,&report))return -200-static_cast<int>(report.failure);
  return report.completed==128&&report.checks==384?384:-201;
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeMethods(
    JNIEnv* env,jclass,jobjectArray methods,jintArray indices,jintArray modifiers,
    jclass owner,jobject carrier,jobject thread,jint direct,jint virtual_count,jint table_candidate) {
  using namespace artdcevm;
  using namespace artdcevm::jni_access;
  if(!methods||!indices||!modifiers||!owner||!carrier||!thread||env->ExceptionCheck())return -300;
  const jsize count=env->GetArrayLength(methods);
  if(count<3||count>32||env->GetArrayLength(indices)!=count||env->GetArrayLength(modifiers)!=count)return -301;
  std::vector<jint> dex(count),flags(count);
  env->GetIntArrayRegion(indices,0,count,dex.data());
  env->GetIntArrayRegion(modifiers,0,count,flags.data());
  if(env->ExceptionCheck())return -302;
  struct Roots {
    JNIEnv* env;std::vector<jobject> values;
    ~Roots(){for(jobject root:values)env->DeleteGlobalRef(root);}
    jobject Own(jobject value){auto root=env->NewGlobalRef(value);if(root)values.push_back(root);return root;}
  } roots{env,{}};
  jobject class_global=roots.Own(owner);
  if(!class_global)return -303;
  last_table_count=-1;
  jobject thread_global=roots.Own(thread);
  if(!thread_global)return -303;
  std::vector<method_identity::Oracle> oracles;
  for(jsize i=0;i<count;++i) {
    jobject method=env->GetObjectArrayElement(methods,i);
    if(!method||env->ExceptionCheck())return -304;
    jobject global=roots.Own(method);env->DeleteLocalRef(method);
    if(!global||env->ExceptionCheck())return -303;
    oracles.push_back({global,static_cast<uint32_t>(dex[i]),static_cast<uint32_t>(flags[i])});
  }
  JavaVM* vm=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK||!vm)return -305;
  std::unique_lock<std::mutex> guard(initialization);
  if(!initialized||!std::strcmp(last_route,"uncalibrated"))return -306;
  ShadowHookEngine hooks;
  auto leaf=reinterpret_cast<abi::common::FromReflectedMethod>(
      hooks.FindSymbol("libart.so",runnable_jni::Bridge::SymbolName()));
  if(!leaf)return -307;
  // This debug-owned class has exactly one private native fixture. Public JNI
  // registration supplies the independent mutable-data oracle; every worker
  // registers the same binding, so no competing binding changes are possible.
  if(table_candidate) {
    JNINativeMethod registration{const_cast<char*>("nativeMarker"),
        const_cast<char*>("()V"),reinterpret_cast<void*>(OwnedNativeMarker)};
    if(env->RegisterNatives(owner,&registration,1)!=JNI_OK||env->ExceptionCheck())return -309;
  }
  Probe probe{env,vm,current(),carrier,thread_global,0,!std::strcmp(last_route,"new-global-ref")};
  MethodProbe request{probe,leaf,oracles.data(),oracles.size(),{},class_global,
      {0,0,static_cast<uint32_t>(direct),static_cast<uint32_t>(virtual_count)},table_candidate!=0,
      reinterpret_cast<void*>(OwnedNativeMarker)};
  guard.unlock();
  auto result=runnable_executor::With(env,thread_global,ObserveMethods,&request);
  if(result!=runnable_jni::Result::kSuccess)return -300-static_cast<int>(result);
  if(request.probe.hits!=1||request.calibration.identities.size()!=oracles.size())return -308;
  return request.calibration.model==method_identity::Model::kMethod11?11:12;
}
#endif

#if defined(ARTDCEVM_TESTING)
namespace artdcevm::jni_access {
bool CalibrateServices(HookEngine& hooks,JNIEnv* env,std::string* error) {
  runtime_services_observed={};
  // The common observer selects fixed source declarations from actual named
  // calls and independently validates the Runtime receiver graph.
  return runtime_services::CalibrateRuntimeServices(
      hooks,env,&runtime_services_observed,error);
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeRuntimeServices(JNIEnv* env,jclass) {
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(!artdcevm::jni_access::CalibrateServices(hooks,env,&error)&&error.empty())
    error="Runtime service calibration failed without diagnostic";
  return env->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeNativeCache(JNIEnv* env,jclass) {
  artdcevm::jni_access::last_cache_threads=0;
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(artdcevm::jni_access::CalibrateServices(hooks,env,&error)) {
    const auto& services=artdcevm::jni_access::runtime_services_observed;
    artdcevm::native_cache::Service cache;
    if(cache.Bind(hooks,services.receivers.thread_list,&error)&&cache.Prepare(&error)) {
      using namespace artdcevm::abi::arm64::android_common; using namespace ::artdcevm::abi::common;
      auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
      auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
      if(!construct||!destroy)error="Missing named owned cache calibration stop";
      else {SuspendAll storage{};construct(&storage,"artdcevm synchronous native cache clearing",false);
        size_t count=0;cache.ClearStoppedWorld(&count,&error);destroy(&storage);
        artdcevm::jni_access::last_cache_threads=count;}
    }
  }
  if(error.empty()&&!artdcevm::jni_access::runtime_services_observed.runtime)error="Runtime service calibration failed without diagnostic";
  return env->NewStringUTF(error.c_str());
}

#endif

#if defined(ARTDCEVM_TESTING)
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeCacheThreads(JNIEnv*,jclass) {
  return static_cast<jint>(artdcevm::jni_access::last_cache_threads);
}
#endif

#if defined(ARTDCEVM_TESTING)
namespace {
artdcevm::retirement::RetiredMethodRegistry debug_retired_entries;
struct RetireEntryRequest {
  void* manager; jmethodID id;
  void* (*decode)(void*,jmethodID);
};
bool RetireEntry(const artdcevm::runnable_jni::Context&,void* data) {
  auto& request=*static_cast<RetireEntryRequest*>(data);
  auto* method=request.decode(request.manager,request.id);
  if(!method)return false;
  debug_retired_entries.Retire(method);
  return debug_retired_entries.Contains(method);
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeMethodGuards(JNIEnv* env,jclass,jclass owner) {
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(!artdcevm::jni_access::CalibrateServices(hooks,env,&error))
    return env->NewStringUTF(error.c_str());
  const auto& services=artdcevm::jni_access::runtime_services_observed;
  auto root=env->NewGlobalRef(owner);
  if(!root)return env->NewStringUTF("Missing owned guard fixture root");
  auto removed=env->GetStaticMethodID(owner,"removed","()I");
  auto kept=env->GetStaticMethodID(owner,"kept","()I");
  auto exception=env->FindClass("java/lang/NoSuchMethodError");
  bool success=removed&&kept&&exception&&!env->ExceptionCheck();
  if(success&&debug_retired_entries.Count()==0&&env->CallStaticIntMethodA(owner,removed,nullptr)!=41)
    success=false;
  if(success)success=artdcevm::retirement::InstallMethodEntryGuards(hooks,env,root,
      services.receivers.jni_id_manager,debug_retired_entries,&error);
  RetireEntryRequest request{services.receivers.jni_id_manager,removed,
      reinterpret_cast<void*(*)(void*,jmethodID)>(hooks.FindSymbol("libart.so",
      "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"))};
  if(success)success=request.decode&&artdcevm::runnable_executor::With(env,root,RetireEntry,&request)==
      artdcevm::runnable_jni::Result::kSuccess;
  if(success) {
    env->CallStaticIntMethodA(owner,removed,nullptr);
    auto thrown=env->ExceptionOccurred();
    env->ExceptionClear();
    success=thrown&&env->IsInstanceOf(thrown,exception);
    if(thrown)env->DeleteLocalRef(thrown);
  }
  if(success)success=env->CallStaticIntMethodA(owner,kept,nullptr)==73&&!env->ExceptionCheck();
  env->DeleteGlobalRef(root);
  if(!success&&error.empty())error="Actual method guard identity/exception/passthrough failed";
  return env->NewStringUTF(error.c_str());
}
#endif


#if defined(ARTDCEVM_TESTING)
namespace {
artdcevm::retirement::RetiredFieldRegistry debug_retired_fields;
jobject field_guard_anchor=nullptr;
thread_local std::unique_ptr<artdcevm::contextual_field_id::Stage> debug_field_stage;
struct RetireFieldsRequest {
  artdcevm::field_mirror::NamedField leaf;
  void (*visit)(void*,void*);
  jobject mirrors[2];
  artdcevm::contextual_field_id::Stage* stage;
};
// android-15.0.0_r1 reflective_value_visitor.h fixed virtual declaration.
// The native original constructs SourceInfo; this visitor never fabricates one.
struct FieldSourceInfo {const void* vptr;uint32_t type;};
class RetireFieldVisitor {
 public:
  virtual ~RetireFieldVisitor()=default;
  virtual void* VisitMethod(void* method,const FieldSourceInfo&){return method;}
  virtual void* VisitField(void* field,const FieldSourceInfo& source) {
    void* object=nullptr;std::memcpy(&object,reinterpret_cast<const unsigned char*>(&source)+16,8);
    if(source.type!=2||object!=expected_mirror||field!=expected_field)return field;
    stage->Retire(field);observed=true;return field;
  }
  artdcevm::contextual_field_id::Stage* stage=nullptr;
  void* expected_mirror=nullptr;void* expected_field=nullptr;bool observed=false;
};
bool RetireFields(const artdcevm::runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<RetireFieldsRequest*>(data);
  // Both targets must be independently validated before retiring either one.
  for(size_t i=0;i<2;++i){void* mirror=nullptr;
    if(!artdcevm::runnable_executor::DecodeStrongGlobal(context,request.mirrors[i],&mirror)||
       request.leaf(mirror)!=artdcevm::contextual_field_id::NativeField(request.stage->proofs[i]))return false;}
  for(size_t i=0;i<2;++i) {
    void* mirror=nullptr;
    if(!artdcevm::runnable_executor::DecodeStrongGlobal(context,request.mirrors[i],&mirror))return false;
    void* field=artdcevm::contextual_field_id::NativeField(request.stage->proofs[i]);
    if(!field||request.leaf(mirror)!=field)return false;
    RetireFieldVisitor visitor;visitor.stage=request.stage;visitor.expected_mirror=mirror;visitor.expected_field=field;
    request.visit(mirror,&visitor);
    uint32_t token=0;std::memcpy(&token,static_cast<unsigned char*>(mirror)+
        artdcevm::field_mirror::kDeclaration.field_index,4);
    if(!visitor.observed||token!=debug_retired_fields.TokenFor(field)||
       !(token&artdcevm::retirement::kRetiredFieldTag)||request.leaf(mirror)!=field)return false;
  }
  return true;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_beginFieldHandleProof(
    JNIEnv* env,jclass,jobject getter,jobject field) {
  std::string error;
  if(!getter||!field||env->ExceptionCheck())return nullptr;
  jfieldID id=env->FromReflectedField(field);
  jobject root=id?env->NewGlobalRef(getter):nullptr;
  const bool success=root&&!env->ExceptionCheck()&&
      artdcevm::retirement::BeginMethodHandleFieldTargetProof(env,root,id,&error);
  if(root)env->DeleteGlobalRef(root);
  if(env->ExceptionCheck())return nullptr;
  if(!success){debug_field_stage.reset();artdcevm::jni_access::ReleaseOwnedFieldProofStage();}
  if(!success&&error.empty())error="Missing public field ID/strong MethodHandle proof anchor";
  return env->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_endFieldHandleProof(
    JNIEnv* env,jclass,jboolean invocation_succeeded) {
  std::string error;
  const bool success=artdcevm::retirement::EndMethodHandleFieldTargetProof(invocation_succeeded==JNI_TRUE,&error);
  if(!success){debug_field_stage.reset();artdcevm::jni_access::ReleaseOwnedFieldProofStage();}
  return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT void JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_releaseFieldProofStages(
    JNIEnv* env,jclass) {
  // Java may fail during warmup or fixture loading after both getter proofs.
  // End both cross-call scopes while this JNI environment is still attached.
  debug_field_stage.reset();
  artdcevm::jni_access::ReleaseOwnedFieldProofStage();
  artdcevm::contextual_field_id::Prune(env,debug_retired_fields);
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_prepareFieldGuards(
    JNIEnv* env,jclass,jclass owner,jobject instance,jobject instance_field,jobject static_field,
    jobject instance_getter,jobject static_getter) {
  using namespace artdcevm;
  ShadowHookEngine hooks;std::string error;
  auto fail=[&](const std::string& value){return env->ExceptionCheck()?nullptr:env->NewStringUTF(value.c_str());};
  if(!owner||!instance||!instance_field||!static_field||!instance_getter||!static_getter||env->ExceptionCheck())
    return fail("Missing actual field MethodHandle preparation oracles");
  if(!jni_access::CalibrateServices(hooks,env,&error))return fail(error);
  const auto& services=jni_access::runtime_services_observed;
  auto removed=env->GetFieldID(owner,"removedField","I");
  auto removed_static=env->GetStaticFieldID(owner,"removedStatic","J");
  if(!removed||!removed_static||env->ExceptionCheck())return fail("Missing public field ID oracles");
  if(env->GetIntField(instance,removed)!=41||env->GetStaticLongField(owner,removed_static)!=13||env->ExceptionCheck())
    return fail("Actual original field values unavailable before retirement");
  if(!field_guard_anchor) {auto object=env->FindClass("java/lang/Object");
    if(object){field_guard_anchor=env->NewGlobalRef(object);env->DeleteLocalRef(object);}}
  if(!field_guard_anchor||env->ExceptionCheck())return fail("Missing durable field guard anchor");
  if(!retirement::InstallFieldEntryGuards(hooks,env,field_guard_anchor,services.receivers.jni_id_manager,
      field_mirror::DebugCalibration(),debug_retired_fields,&error)||
      !retirement::RegisterMethodHandleFieldOwner(debug_retired_fields,&error))return fail(error);
  jobject instance_root=env->NewGlobalRef(instance_getter),static_root=env->NewGlobalRef(static_getter);
  struct Roots {JNIEnv* env;jobject instance,statik;~Roots(){if(instance)env->DeleteGlobalRef(instance);if(statik)env->DeleteGlobalRef(statik);}}
      roots{env,instance_root,static_root};
  if(!instance_root||!static_root||env->ExceptionCheck())return fail("Missing strong field MethodHandle oracles");
  debug_field_stage.reset();
  if(!contextual_field_id::Initialize(hooks,&error))return fail(error);
  auto stage=std::make_unique<contextual_field_id::Stage>(env,debug_retired_fields);
  if(!stage->Add(instance_field,removed,&error)||!stage->Add(static_field,removed_static,&error))return fail(error);
  const std::vector<retirement::FieldMethodHandleOracle> oracles{{instance_root,removed,stage->proofs[0],8},{static_root,removed_static,stage->proofs[1],10}};
  if(!retirement::PrepareMethodHandleFieldGuards(hooks,env,services.receivers.jni_id_manager,
      debug_retired_fields,oracles,&error))return fail(error);
  debug_field_stage=std::move(stage);
  // Java must execute both actual original getters before the separate retire
  // entry accepts same-owner readiness. Installation alone never permits it.
  return fail("");
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeFieldGuards(
    JNIEnv* env,jclass,jclass owner,jobject instance,jobject instance_field,jobject static_field) {
  artdcevm::ShadowHookEngine hooks;std::string error;
  if(env->ExceptionCheck())return nullptr;
  if(!owner||!instance||!instance_field||!static_field)return env->NewStringUTF("Missing actual field retirement fixture");
  struct StageRelease{~StageRelease(){debug_field_stage.reset();}} stage_release;
  if(!debug_field_stage||debug_field_stage->proofs.size()!=2||!artdcevm::retirement::MethodHandleFieldGuardsReady(debug_retired_fields))
    return env->NewStringUTF("Actual original instance/static MethodHandle calls have not proved field guards");
  auto removed=env->GetFieldID(owner,"removedField","I");
  auto kept=env->GetFieldID(owner,"keptField","I");
  auto removed_static=env->GetStaticFieldID(owner,"removedStatic","J");
  auto kept_static=env->GetStaticFieldID(owner,"keptStatic","J");
  artdcevm::contextual_field_id::Stage kept_proofs{env,debug_retired_fields};
  auto kept_mirror=kept?env->ToReflectedField(owner,kept,JNI_FALSE):nullptr;
  auto kept_static_mirror=kept_static?env->ToReflectedField(owner,kept_static,JNI_TRUE):nullptr;
  bool kept_proven=kept_mirror&&kept_static_mirror&&
      kept_proofs.Add(kept_mirror,kept,&error)&&kept_proofs.Add(kept_static_mirror,kept_static,&error);
  if(kept_mirror)env->DeleteLocalRef(kept_mirror);if(kept_static_mirror)env->DeleteLocalRef(kept_static_mirror);
  auto exception=env->FindClass("java/lang/NoSuchFieldError");
  auto instance_root=env->NewGlobalRef(instance_field),static_root=env->NewGlobalRef(static_field);
  if(!field_guard_anchor) {auto object=env->FindClass("java/lang/Object");
    if(object)field_guard_anchor=env->NewGlobalRef(object);}
  bool success=kept_proven&&removed&&kept&&removed_static&&kept_static&&exception&&instance_root&&static_root&&
      field_guard_anchor&&!env->ExceptionCheck();
  RetireFieldsRequest request{
    reinterpret_cast<artdcevm::field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv")),
    reinterpret_cast<void(*)(void*,void*)>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11VisitTargetEPNS_22ReflectiveValueVisitorE")),
    {instance_root,static_root},debug_field_stage.get()};
  if(success)success=request.leaf&&request.visit&&artdcevm::runnable_executor::With(env,field_guard_anchor,
      RetireFields,&request)==artdcevm::runnable_jni::Result::kSuccess;
  auto rejected=[&]() {auto thrown=env->ExceptionOccurred();env->ExceptionClear();
    bool matches=thrown&&env->IsInstanceOf(thrown,exception);if(thrown)env->DeleteLocalRef(thrown);return matches;};
  if(success) {env->GetIntField(instance,removed);success=rejected();}
  if(success) {env->SetIntField(instance,removed,99);success=rejected();}
  if(success) {env->GetStaticLongField(owner,removed_static);success=rejected();}
  if(success) {env->SetStaticLongField(owner,removed_static,99);success=rejected();}
  if(success){auto fresh=env->GetFieldID(owner,"keptField","I");auto fresh_static=env->GetStaticFieldID(owner,"keptStatic","J");
    success=fresh&&fresh_static&&!env->ExceptionCheck()&&
      !artdcevm::contextual_field_id::Retired(env,instance,false,fresh,debug_retired_fields)&&
      !artdcevm::contextual_field_id::Retired(env,owner,true,fresh_static,debug_retired_fields)&&
      env->GetIntField(instance,fresh)==73&&env->GetStaticLongField(owner,fresh_static)==17&&!env->ExceptionCheck();}
  if(success)success=env->GetIntField(instance,kept)==73&&env->GetStaticLongField(owner,kept_static)==17&&!env->ExceptionCheck();
  if(success) {env->SetIntField(instance,kept,79);env->SetStaticLongField(owner,kept_static,19);
    success=env->GetIntField(instance,kept)==79&&env->GetStaticLongField(owner,kept_static)==19&&!env->ExceptionCheck();
    env->SetIntField(instance,kept,73);env->SetStaticLongField(owner,kept_static,17);}
  if(instance_root)env->DeleteGlobalRef(instance_root);if(static_root)env->DeleteGlobalRef(static_root);
  if(!success&&error.empty())error="Actual field guard token/exception/passthrough failed";
  return env->NewStringUTF(error.c_str());
}
namespace artdcevm::method_identity {
Calibration DebugCalibration(){return jni_access::last_method_model;}
}
#endif

#if defined(ARTDCEVM_TESTING)
namespace {
struct AllocatorRequest {
 artdcevm::HookEngine* hooks;void* linker;std::vector<jobject> fields,methods;
 jobject boot_method;artdcevm::field_mirror::NamedField field;
 artdcevm::abi::common::FromReflectedMethod method;
 std::vector<void*> owned,foreign;artdcevm::retirement::Membership proof;std::string error;
};
bool CollectAllocatorMembers(const artdcevm::runnable_jni::Context& context,void* data) {
 auto& r=*static_cast<AllocatorRequest*>(data);
 for(auto root:r.fields){void* mirror=nullptr;if(!artdcevm::runnable_executor::DecodeStrongGlobal(context,root,&mirror))return false;
  void* field=r.field(mirror);if(!field)return false;r.owned.push_back(field);}
 auto scope=artdcevm::abi::common::RunnableJniScope{context.self,context.env,context.vm};
 for(auto root:r.methods){void* method=r.method(scope,root);if(!method)return false;r.owned.push_back(method);}
 void* foreign=r.method(scope,r.boot_method);if(!foreign)return false;r.foreign.push_back(foreign);return true;
}
bool CalibrateAllocator(const artdcevm::exclusive_owned_roots::Context& context,void* data) {
 auto& r=*static_cast<AllocatorRequest*>(data);
 if(!artdcevm::retirement::CalibrateAllocatorMembership(*r.hooks,r.linker,context,r.owned,r.foreign,&r.proof,&r.error))return false;
 debug_retired_fields.Retire(r.owned.front());
 debug_retired_entries.Retire(r.owned.back());return true;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeAllocatorCleanup(JNIEnv* env,jclass,
 jclass owner,jobject loader,jobjectArray fields,jobjectArray methods,jobject boot_method) {
 artdcevm::ShadowHookEngine hooks;std::string error;
 if(!artdcevm::jni_access::CalibrateServices(hooks,env,&error))return env->NewStringUTF(error.c_str());
 const auto& services=artdcevm::jni_access::runtime_services_observed;
 std::vector<jobject> globals;auto own=[&](jobject value){auto root=env->NewGlobalRef(value);if(root)globals.push_back(root);return root;};
 jobject roots[]={own(loader),own(owner)};
 AllocatorRequest request{&hooks,services.receivers.class_linker,{},{},own(boot_method),
  reinterpret_cast<artdcevm::field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv")),
  reinterpret_cast<artdcevm::abi::common::FromReflectedMethod>(hooks.FindSymbol("libart.so",artdcevm::abi::common::kFromReflectedMethod)),{},{},{},{}};
 bool success=roots[0]&&roots[1]&&request.boot_method&&request.field&&request.method&&!env->ExceptionCheck();
 for(jobjectArray array:{fields,methods}) {
  if(!array){success=false;break;}
  for(jsize i=0;i<env->GetArrayLength(array);++i){auto value=env->GetObjectArrayElement(array,i);auto root=own(value);
   if(value)env->DeleteLocalRef(value);if(!root){success=false;break;}(array==fields?request.fields:request.methods).push_back(root);}
 }
 if(success)success=!request.fields.empty()&&!request.methods.empty()&&artdcevm::runnable_executor::With(env,roots[1],CollectAllocatorMembers,&request)==artdcevm::runnable_jni::Result::kSuccess;
 artdcevm::exclusive_owned_roots::Service stop;
 if(success)success=artdcevm::exclusive_owned_roots::Service::Bind(hooks,&stop,&error)&&stop.With(env,roots,2,CalibrateAllocator,&request,&error);
 if(success)success=artdcevm::retirement::InstallRetirementCleanup(hooks,request.proof,debug_retired_fields,debug_retired_entries,&error);
 for(auto root:globals)env->DeleteGlobalRef(root);
 if(!success&&error.empty())error=request.error.empty()?"Owned allocator membership/cleanup installation failed":request.error;
 return env->NewStringUTF(error.c_str());
}
extern "C" JNIEXPORT jintArray JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_cleanupCounts(JNIEnv* env,jclass) {
 jint values[]={static_cast<jint>(artdcevm::retirement::CleanupCalls()),static_cast<jint>(artdcevm::retirement::CleanupFields()),
  static_cast<jint>(artdcevm::retirement::CleanupMethods()),static_cast<jint>(debug_retired_fields.Count()),static_cast<jint>(debug_retired_entries.Count())};
 auto result=env->NewIntArray(5);if(result)env->SetIntArrayRegion(result,0,5,values);return result;
}
#endif

#ifdef ARTDCEVM_TESTING
namespace artdcevm::jni_access {
retirement::RetiredMethodRegistry& OwnedMethodRegistry(){return debug_retired_entries;}
retirement::RetiredFieldRegistry& OwnedFieldRegistry(){return debug_retired_fields;}
}
#endif
