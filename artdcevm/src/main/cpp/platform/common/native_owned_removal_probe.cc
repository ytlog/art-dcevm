#ifdef ARTDCEVM_TESTING
#include "platform/jni_access/owned_evolution_proofs.h"
#include "platform/common/native_method_table.h"
#include "platform/common/native_structural_reflection.h"
#include "platform/common/native_owned_removal.h"
#include "platform/common/modern_source_commit.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/native_member_provider.h"
#include "hooks/shadow_hook_engine.h"
#include <jvmti.h>
#include "platform/common/contextual_field_stage.h"
namespace {
using namespace artdcevm;
namespace reflection=native_structural_reflection;
thread_local native_source_commit::Diagnostics completed_diagnostics;
thread_local std::unique_ptr<contextual_field_id::Stage> owned_getter_stage;
bool Retired(void* method){return jni_access::OwnedMethodRegistry().Contains(method);}
bool RegistryContains(void*,void* method){return Retired(method);}
void RegistryRetire(void*,void* method){jni_access::OwnedMethodRegistry().Retire(method);}
bool FieldRetired(void*,void* field){return jni_access::OwnedFieldRegistry().TokenFor(field)!=0;}
void FieldRetire(void*,void* field);
struct State {
 State(HookEngine* h,jni_access::EvolutionProofs* p):hooks(h),proofs(p){}
 HookEngine* hooks;
 jni_access::EvolutionProofs* proofs;
 native_cache::Service cache;
 reflection::Service reflection;
 native_member_provider::Service provider;
 bool field_deletion=false;
 contextual_field_id::Stage* field_stage=nullptr;
 native_source_commit::Diagnostics diagnostics;
};
void FieldRetire(void* raw,void* field){static_cast<State*>(raw)->field_stage->Retire(field);}
bool Ready(void* raw){auto& state=*static_cast<State*>(raw);auto& p=*state.proofs;
 return p.methods&&p.mirrors&&p.metadata.Calibrated()&&p.fields.Calibrated()&&
   p.observed.runtime&&p.observed.instrumentation&&p.observed.receivers.class_linker&&
   p.observed.receivers.jit&&p.observed.receivers.jit_code_cache&&p.observed.receivers.heap&&
   p.observed.receivers.thread_list&&state.cache.PreparedForCurrentThread()&&jni_access::OwnedMethodRegistry().Count()>0&&
   (!state.field_deletion||(jni_access::OwnedFieldRegistry().Count()>0&&retirement::MethodHandleFieldGuardsReady(jni_access::OwnedFieldRegistry())&&
    retirement::FieldVarHandleGuardsReady(jni_access::OwnedFieldRegistry())));
}
bool MethodHandlesReady(void*){return retirement::MethodHandleGuardsReady(jni_access::OwnedMethodRegistry());}
bool FieldMethodHandlesReady(void*){return retirement::MethodHandleFieldGuardsReady(jni_access::OwnedFieldRegistry());}
bool FieldVarHandlesReady(void*){return retirement::FieldVarHandleGuardsReady(jni_access::OwnedFieldRegistry());}
std::unique_ptr<source_commit::Services> Prepare(const source_commit::Plan& plan,void* raw,std::string* error){
 auto& state=*static_cast<State*>(raw);auto& p=*state.proofs;
 if(state.field_deletion){std::vector<void*> original_fields;
  for(auto klass:plan.old_classes)for(bool statik:{false,true}){field_array::View fields;
   if(!p.metadata.Fields(klass,statik,&fields,error))return nullptr;
   for(uint32_t i=0;i<fields.count;++i)if(fields.Selected(i)){
    void* field=fields.At(i);original_fields.push_back(field);
    if(!state.field_stage||!state.field_stage->Contains(field)||p.metadata.FieldDeclaringClass(field)!=klass){if(error)*error="Original native field lacks exact public contextual proof";return nullptr;}}}
  if(!state.field_stage->Covers(original_fields)){if(error)*error="Public contextual field set differs from complete native table";return nullptr;}
 }
 native_member_provider::Dependencies d;d.hooks=state.hooks;d.methods=p.methods.get();d.method_model=&p.method_model;
 d.metadata=&p.metadata;d.fields=&p.fields;d.cache=&state.cache;d.runtime=p.observed;d.reflection=&state.reflection;
 d.retirement={&state,RegistryContains,FieldRetired,RegistryRetire,FieldRetire};d.is_retired=Retired;
 d.allow_field_removal=state.field_deletion;
 d.expected_removal=state.field_deletion?native_member_provider::Removal::kFieldsOnly:native_member_provider::Removal::kMethodsOnly;
 d.handle_guards={&state,MethodHandlesReady,FieldVarHandlesReady,FieldMethodHandlesReady};d.diagnostics=&state.diagnostics;
 return state.provider.Prepare(plan,d,error);
}
}
namespace {
jstring RunDeletion(JNIEnv* env,jclass suite,jclass target,jobject instance,jbyteArray dex,bool field_deletion,jobjectArray handles=nullptr,jint baseline=0){
 using namespace artdcevm;
 auto fail=[&](const std::string& error){return env->ExceptionCheck()?nullptr:env->NewStringUTF(error.c_str());};
 struct GetterRelease{bool fields;~GetterRelease(){if(fields)owned_getter_stage.reset();}} getter_release{field_deletion};
 if(!target||!instance||!dex||(!field_deletion&&(!handles||env->GetArrayLength(handles)!=3))||env->ExceptionCheck())return fail("Invalid owned deletion fixture");
 if(field_deletion&&(!retirement::MethodHandleFieldGuardsReady(jni_access::OwnedFieldRegistry())||
    !retirement::FieldVarHandleGuardsReady(jni_access::OwnedFieldRegistry())))return fail(
    "Owned field handles have not observed both independently calibrated targets through actual original dispatch: fieldMH="+
    std::to_string(retirement::MethodHandleFieldGuardsReady(jni_access::OwnedFieldRegistry()))+
    ",fieldVH="+std::to_string(retirement::FieldVarHandleGuardsReady(jni_access::OwnedFieldRegistry()))+
    "; "+retirement::DescribeFieldVarHandleGuards(jni_access::OwnedFieldRegistry()));
 auto& proofs=jni_access::OwnedEvolutionProofs();
 completed_diagnostics={};
 struct Roots {JNIEnv* env;std::vector<jobject> values;~Roots(){for(auto value:values)env->DeleteGlobalRef(value);}} roots{env,{}};
 if(!field_deletion)for(jsize n=0;n<3;++n){jobject local=env->GetObjectArrayElement(handles,n);if(!local)return nullptr;
  jobject root=env->NewGlobalRef(local);env->DeleteLocalRef(local);if(!root)return nullptr;roots.values.push_back(root);}


 ShadowHookEngine hooks;std::string error;State state{&hooks,&proofs};state.field_deletion=field_deletion;
 contextual_field_id::Stage field_stage{env,jni_access::OwnedFieldRegistry()};state.field_stage=&field_stage;
 if(field_deletion){
  if(!contextual_field_id::Initialize(hooks,&error))return fail(error);
  jclass klass=env->FindClass("java/lang/Class");
  jmethodID declared=klass?env->GetMethodID(klass,"getDeclaredFields","()[Ljava/lang/reflect/Field;"):nullptr;
  jobjectArray fields=declared?static_cast<jobjectArray>(env->CallObjectMethod(target,declared)):nullptr;
  if(klass)env->DeleteLocalRef(klass);
  if(!fields||env->ExceptionCheck())return fail("Original public field enumeration unavailable");
  bool prepared=true;const jsize count=env->GetArrayLength(fields);
  for(jsize i=0;i<count&&prepared;i++){jobject field=env->GetObjectArrayElement(fields,i);
   jfieldID id=field?env->FromReflectedField(field):nullptr;
   prepared=id&&!env->ExceptionCheck()&&field_stage.Add(field,id,&error);if(field)env->DeleteLocalRef(field);}
  env->DeleteLocalRef(fields);if(!prepared)return fail(error);
 }
 if(!proofs.mirrors||!state.reflection.Bind(hooks,proofs.observed.runtime,*proofs.mirrors,&error)||
    !state.cache.Bind(hooks,proofs.observed.receivers.thread_list,&error)||!state.cache.Prepare(&error)||!Ready(&state))
   return fail(error.empty()?"Owned deletion prerequisites unavailable":error);
 // Missing structural services must reject before source verification permits
 // removal, so the original implementation can never encounter deleted members.
 const char* symbols[]={"_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE",
   "_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE",
   "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv",
   "_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv",
   "_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEPKc"};
 for(const char* symbol:symbols)if(!hooks.FindSymbol("libart.so",symbol))return fail(std::string("Missing owned deletion service: ")+symbol);
 if(!hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE"))
   return fail("Missing owned source heap replacement");
 JavaVM* vm=nullptr;jvmtiEnv* jvmti=nullptr;
 if(env->GetJavaVM(&vm)!=JNI_OK||vm->GetEnv(reinterpret_cast<void**>(&jvmti),JVMTI_VERSION_1_2)!=JNI_OK||!jvmti)
   return fail("Public owned JVMTI environment unavailable");
 struct Dispose{jvmtiEnv* env;~Dispose(){env->DisposeEnvironment();}} dispose{jvmti};
 jvmtiCapabilities capabilities{};capabilities.can_redefine_classes=1;
 if(jvmti->AddCapabilities(&capabilities)!=JVMTI_ERROR_NONE)return fail("Owned structural capability unavailable");
 jmethodID deleted=nullptr;jfieldID removed=nullptr,removed_static=nullptr,kept_field=nullptr,kept_static=nullptr;
 if(field_deletion){
   removed=env->GetFieldID(target,"i","I");removed_static=env->GetStaticFieldID(target,"si","I");
   kept_field=env->GetFieldID(target,"keep","I");kept_static=env->GetStaticFieldID(target,"staticKeep","I");
   if(!removed||!removed_static||!kept_field||!kept_static||env->ExceptionCheck())return nullptr;
 }else{
 deleted=env->GetMethodID(target,"addedMethod","()I");
 jmethodID kept=env->GetMethodID(target,"compute","()I");
 jclass fixture=env->FindClass("com/github/ytlog/artdcevm/testing/RetiredEntryFixture");
 jmethodID statik=fixture?env->GetStaticMethodID(fixture,"kept","()I"):nullptr;
 if(fixture)env->DeleteLocalRef(fixture);
 if(!deleted||!kept||!statik||env->ExceptionCheck())return nullptr;
 std::vector<retirement::MethodHandleOracle> oracles{{roots.values[0],deleted},{roots.values[1],kept},{roots.values[2],statik}};
 if(!retirement::PrepareMethodHandleGuards(hooks,env,proofs.observed.receivers.jni_id_manager,
     jni_access::OwnedMethodRegistry(),oracles,&error))return fail(error);
 jmethodID actual=env->GetStaticMethodID(suite,"invokeOwnedMethodHandle","(Ljava/lang/invoke/MethodHandle;Ljava/lang/Object;)I");
 if(!actual||env->ExceptionCheck()||!retirement::BeginMethodHandleTargetProof(env,roots.values[0],deleted,&error))return fail(error);
 jint value=env->CallStaticIntMethod(suite,actual,roots.values[0],instance);
 const bool invocation=!env->ExceptionCheck()&&value==baseline;
 if(!retirement::EndMethodHandleTargetProof(invocation,&error))return fail(error);
 }
 std::vector<unsigned char> bytes(static_cast<size_t>(env->GetArrayLength(dex)));
 env->GetByteArrayRegion(dex,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));
 if(env->ExceptionCheck())return nullptr;
 native_owned_removal::Provider provider{Ready,Prepare,&proofs.metadata,&state,field_deletion};
 if(!native_owned_removal::Install(hooks,&error))return fail(error);
 auto result=native_owned_removal::Run(hooks,env,jvmti,target,bytes,provider,&error);
 if(result!=JVMTI_ERROR_NONE)return fail("Owned structural error "+std::to_string(result)+": "+error);
 completed_diagnostics=state.diagnostics;
 jclass expected=env->FindClass(field_deletion?"java/lang/NoSuchFieldError":"java/lang/NoSuchMethodError");
 if(!expected||env->ExceptionCheck())return nullptr;
 auto rejected=[&](){jthrowable pending=env->ExceptionOccurred();env->ExceptionClear();
   const bool match=pending&&env->IsInstanceOf(pending,expected);
   if(pending)env->DeleteLocalRef(pending);return match;};
 bool success;
 if(field_deletion){
   env->GetIntField(instance,removed);success=rejected();
   if(success){env->SetIntField(instance,removed,99);success=rejected();}
   if(success){env->GetStaticIntField(target,removed_static);success=rejected();}
   if(success){env->SetStaticIntField(target,removed_static,99);success=rejected();}
   if(success){auto fresh=env->GetFieldID(target,"keep","I");auto fresh_static=env->GetStaticFieldID(target,"staticKeep","I");
    success=fresh&&fresh_static&&!env->ExceptionCheck()&&
      !contextual_field_id::Retired(env,instance,false,fresh,jni_access::OwnedFieldRegistry())&&
      !contextual_field_id::Retired(env,target,true,fresh_static,jni_access::OwnedFieldRegistry())&&
      env->GetIntField(instance,fresh)==41&&env->GetStaticIntField(target,fresh_static)==42&&!env->ExceptionCheck();}
   if(success)success=env->GetIntField(instance,kept_field)==41&&env->GetStaticIntField(target,kept_static)==42&&!env->ExceptionCheck();
 }else{env->CallIntMethodA(instance,deleted,nullptr);success=rejected();}
 env->DeleteLocalRef(expected);
 if(!success)return fail("Retired JNI entry rejection or kept field preservation failed");
 return fail("");
}
}
namespace artdcevm::jni_access {void ReleaseOwnedFieldProofStage(){owned_getter_stage.reset();}}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeOwnedMethodDeletion(
 JNIEnv* env,jclass suite,jclass target,jobject instance,jbyteArray dex,jobjectArray handles,jint baseline){return RunDeletion(env,suite,target,instance,dex,false,handles,baseline);}
namespace {
struct JitMethodState {
 jobject method;void* cache;artdcevm::abi::arm64::android_common::FromReflectedMethod leaf;
 bool (*contains)(void*,void*);int value=0;
};
bool ObserveJitMethod(const artdcevm::runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<JitMethodState*>(raw);
 const artdcevm::abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 void* method=request.leaf(scope,request.method);
 if(!method)return false;request.value=request.contains(request.cache,method)?2:1;return true;
}
struct FieldGuardOracles {
 void* manager;artdcevm::field_mirror::NamedField leaf;
 jobject mirrors[2];jfieldID ids[2];void* fields[2]={nullptr,nullptr};
 std::shared_ptr<artdcevm::contextual_field_id::Proof> identities[2];
};
bool ObserveFieldGuardOracles(const artdcevm::runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<FieldGuardOracles*>(raw);
 for(size_t i=0;i<2;i++) {
   void* mirror=nullptr;
   if(!artdcevm::runnable_executor::DecodeStrongGlobal(context,request.mirrors[i],&mirror))return false;
   void* field=request.leaf(mirror);
   if(!field||field!=artdcevm::contextual_field_id::NativeField(request.identities[i]))return false;
   request.fields[i]=field;
 }
 return request.fields[0]!=request.fields[1];
}
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeOwnedJitMethodState(JNIEnv* env,jclass,jobject method){
 using namespace artdcevm;
 ShadowHookEngine hooks;auto root=method?env->NewGlobalRef(method):nullptr;
 if(!root||env->ExceptionCheck())return -1;
 JitMethodState request{root,jni_access::OwnedEvolutionProofs().observed.receivers.jit_code_cache,
   reinterpret_cast<abi::arm64::android_common::FromReflectedMethod>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kFromReflectedMethod)),
   reinterpret_cast<bool(*)(void*,void*)>(hooks.FindSymbol("libart.so","_ZN3art3jit12JitCodeCache14ContainsMethodEPNS_9ArtMethodE"))};
 const auto result=request.cache&&request.leaf&&request.contains?
   runnable_executor::With(env,root,ObserveJitMethod,&request):runnable_jni::Result::kInvalidRequest;
 env->DeleteGlobalRef(root);return result==runnable_jni::Result::kSuccess?request.value:-1;
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_prepareOwnedFieldDeletionGuards(
 JNIEnv* env,jclass,jclass owner,jobject instance_handle,jobject static_handle,jobject instance_field,jobject static_field,
 jobject instance_getter,jobject static_getter){
 using namespace artdcevm;
 ShadowHookEngine hooks;std::string error;
 auto fail=[&](const std::string& message){return env->ExceptionCheck()?nullptr:env->NewStringUTF(message.c_str());};
 if(!owner||!instance_handle||!static_handle||!instance_field||!static_field||!instance_getter||!static_getter||env->ExceptionCheck())return fail("Invalid owned field guard oracles");
 if(!retirement::FieldEntryGuardsOwn(jni_access::OwnedFieldRegistry()))return fail("Owned field entry guards unavailable");
 std::vector<jobject> roots;
 auto own=[&](jobject value){jobject root=env->NewGlobalRef(value);if(root)roots.push_back(root);return root;};
 struct Roots {JNIEnv* env;std::vector<jobject>& roots;~Roots(){for(auto root:roots)env->DeleteGlobalRef(root);}} release{env,roots};
 jobject target=own(owner),handles[2]={own(instance_handle),own(static_handle)},getters[2]={own(instance_getter),own(static_getter)};
 FieldGuardOracles request{jni_access::OwnedEvolutionProofs().observed.receivers.jni_id_manager,
   reinterpret_cast<field_mirror::NamedField>(hooks.FindSymbol("libart.so","_ZN3art6mirror5Field11GetArtFieldEv")),
   {own(instance_field),own(static_field)},{env->GetFieldID(owner,"i","I"),env->GetStaticFieldID(owner,"si","I")},{nullptr,nullptr},{nullptr,nullptr}};
 if(!target||!handles[0]||!handles[1]||!getters[0]||!getters[1]||!request.mirrors[0]||!request.mirrors[1]||!request.ids[0]||!request.ids[1]||
    !request.manager||!request.leaf||env->ExceptionCheck())return fail("Missing independent field mirror/ID oracle");
 owned_getter_stage.reset();
 auto stage=std::make_unique<contextual_field_id::Stage>(env,jni_access::OwnedFieldRegistry());
 if(!contextual_field_id::Initialize(hooks,&error))return fail(error);
 for(size_t i=0;i<2;i++){if(!stage->Add(request.mirrors[i],request.ids[i],&error))return fail(error);request.identities[i]=stage->proofs.back();}
 if(runnable_executor::With(env,target,ObserveFieldGuardOracles,&request)!=runnable_jni::Result::kSuccess)return fail("Field mirror and independent JNI ID disagree");
 std::vector<retirement::FieldVarHandleOracle> oracles{{handles[0],request.fields[0],request.mirrors[0]},{handles[1],request.fields[1],request.mirrors[1]}};
 if(!retirement::InstallFieldVarHandleGuards(hooks,env,oracles,jni_access::OwnedFieldRegistry(),&error))return fail(error);
 const std::vector<retirement::FieldMethodHandleOracle> getter_oracles{{getters[0],request.ids[0],request.identities[0]},{getters[1],request.ids[1],request.identities[1]}};
 if(!retirement::PrepareMethodHandleFieldGuards(hooks,env,request.manager,jni_access::OwnedFieldRegistry(),getter_oracles,&error))return fail(error);
 owned_getter_stage=std::move(stage);
 return fail("");
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeOwnedFieldDeletion(
 JNIEnv* env,jclass suite,jclass target,jobject instance,jbyteArray dex){return RunDeletion(env,suite,target,instance,dex,true);}
extern "C" JNIEXPORT jlong JNICALL
Java_com_github_ytlog_artdcevm_testing_RuntimeAccessSuite_probeOwnedCommitDiagnostics(JNIEnv*,jclass){
 return jlong(completed_diagnostics.cleared_threads)|(completed_diagnostics.declaring_roots_verified?(jlong(1)<<32):0);
}
#endif
