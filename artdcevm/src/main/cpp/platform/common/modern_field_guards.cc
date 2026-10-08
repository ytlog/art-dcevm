#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/var_handle_field_target.h"
#include "platform/common/modern_field_metadata.h"
#include <memory>
#include "platform/common/jni_field_dispatch.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <atomic>
#include <cstdlib>
#include <type_traits>
#include <vector>
namespace artdcevm::retirement {
namespace {
using Throw=void (*)(void*,const char*,const char*);
struct State {RetiredFieldRegistry* registry=nullptr;jobject anchor=nullptr;
  void* manager=nullptr;Throw throw_exception=nullptr;};
State state;
bool installed=false;
thread_local bool probing=false;
std::atomic<size_t> rejected{0};
void* original_field=nullptr;
void* original_visit=nullptr;
void* GetField(void* mirror) {
  reflection_metadata::FieldIdentity identity;
  if(!reflection_metadata::ReadField(field_mirror::kDeclaration,mirror,
      field_mirror::kDeclaration.field_size,&identity))std::abort();
  if(identity.array_index&kRetiredFieldTag) {
    void* field=state.registry->Resolve(identity.array_index);
    if(!field)std::abort(); // A live tagged mirror roots the owning obsolete Class.
    return field;
  }
  return reinterpret_cast<field_mirror::NamedField>(original_field)(mirror);
}
void VisitField(void* mirror,void* visitor) {
  void* before=GetField(mirror);
  reinterpret_cast<void(*)(void*,void*)>(original_visit)(mirror,visitor);
  // The original supplies the native HeapReflectiveSourceInfo and performs
  // surviving-member metadata/barrier updates. The real mapper can retire a
  // field during its callback; mark afterward, before managed root replacement.
  const uint32_t token=state.registry->TokenFor(before);
  if(token)std::memcpy(static_cast<unsigned char*>(mirror)+field_mirror::kDeclaration.field_index,&token,4);
}
struct Request {jfieldID id=nullptr;jobject mirror=nullptr;bool guarded=false;};
bool Observe(const runnable_jni::Context& context,void* data) {
  auto& request=*static_cast<Request*>(data);
  void* field=nullptr;
  if(request.mirror) {
    void* mirror=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(context,request.mirror,&mirror))return false;
    field=GetField(mirror);
  } else return false;
  if(!field)return false;
  request.guarded=state.registry->TokenFor(field)!=0;
  if(request.guarded) {
    ++rejected;
    state.throw_exception(context.self,"Ljava/lang/NoSuchFieldError;","Field removed by ART DCEVM");
  }
  return true;
}
bool Guard(JNIEnv* env,jfieldID id,jobject reflected) {
  if(probing||state.registry->Count()==0)return false;
  if(env->ExceptionCheck())return true; // Keep the caller's existing exception.
  struct Probe {Probe(){probing=true;}~Probe(){probing=false;}} probe;
  jobject global=reflected?env->NewGlobalRef(reflected):nullptr;
  Request request{id,global,false};
  auto result=reflected&&!global?runnable_jni::Result::kInvalidRequest:
      runnable_executor::With(env,global?global:state.anchor,Observe,&request);
  if(global)env->DeleteGlobalRef(global);
  if(request.guarded)return true;
  if(result==runnable_jni::Result::kSuccess)return false;
  // Never delegate a potentially stale ID when owned access fails. Preserve a
  // bridge/native exception; otherwise report the unavailable guard explicitly.
  if(!env->ExceptionCheck()) {
    auto error=env->FindClass("java/lang/IllegalStateException");
    if(error)env->ThrowNew(error,"ART DCEVM retired field guard lost owned access");
  }
  return true;
}
bool GuardJni(JNIEnv* env,jobject receiver,bool is_static,jfieldID id){
 if(probing||state.registry->Count()==0)return false;
 if(env->ExceptionCheck())return true;
 struct Probe {Probe(){probing=true;}~Probe(){probing=false;}} probe;
 contextual_field_id::Prune(env,*state.registry);
 if(contextual_field_id::Retired(env,receiver,is_static,id,*state.registry)){
  ++rejected;auto error=env->FindClass("java/lang/NoSuchFieldError");
  if(error){env->ThrowNew(error,"Field removed by ART DCEVM");env->DeleteLocalRef(error);}return true;
 }
 if(env->ExceptionCheck())return true;
 std::string error;
 void* field=contextual_field_id::DecodeLive(env,receiver,is_static,id,&error);
 if(!field){if(!env->ExceptionCheck()){auto type=env->FindClass("java/lang/IllegalStateException");if(type){env->ThrowNew(type,error.c_str());env->DeleteLocalRef(type);}}return true;}
 // Ordinary live IDs retain all original ART/CheckJNI checks. Only a public
 // reflected identity matching the same retirement registry is rejected.
 if(!state.registry->TokenFor(field))return false;
 ++rejected;auto type=env->FindClass("java/lang/NoSuchFieldError");if(type){env->ThrowNew(type,"Field removed by ART DCEVM");env->DeleteLocalRef(type);}return true;
}
bool GuardNativeField(void* self,void* field,void* result=nullptr) {
  if(!state.registry->TokenFor(field))return false;
  if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}
  ++rejected;state.throw_exception(self,"Ljava/lang/NoSuchFieldError;","Field removed by ART DCEVM");return true;
}
using NativeCurrent=void* (*)();
struct VarState {
  std::unique_ptr<var_handle_field::Reader> reader;
  RetiredFieldRegistry* registry=nullptr;
  NativeCurrent current=nullptr;
  std::atomic<uint64_t> observed{0};
  uint64_t complete=0,generation=0;
  void* access_target=nullptr;
  abi::arm64::android11::DecodeJObject decode_class=nullptr;
  modern_field_metadata::FieldMetadata metadata;
  jobject instance_class=nullptr,static_class=nullptr;
  std::atomic<uint64_t> entries{0},field_entries{0},other_entries{0},target_seen{0},
      original_true{0},original_false{0},context_failures{0},class_failures{0};
};
VarState vars;
void* original_var_access=nullptr;
bool vars_installed=false;
thread_local void* var_access_self=nullptr;
void* VarTarget(void*,void* handle){
  void* field=vars.reader->Target(handle);
  const auto& fields=vars.reader->Fields();
  for(size_t i=0;i<fields.size();++i)if(fields[i]==field)vars.target_seen.fetch_or(uint64_t{1}<<i,std::memory_order_relaxed);
  return field;
}
bool VarRetired(void*,void* field){return vars.registry->TokenFor(field)!=0;}
void VarReject(void*,void* result){
  if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}
  ++rejected;state.throw_exception(vars.current(),"Ljava/lang/NoSuchFieldError;","Field removed by ART DCEVM");
}
void VarUnavailable(void*){state.throw_exception(vars.current(),"Ljava/lang/IllegalStateException;","ART DCEVM VarHandle target lost calibrated native access");}
void VarObserved(void*,void* field){
  if(!var_access_self||vars.current()!=var_access_self)return;
  const auto& fields=vars.reader->Fields();
  for(size_t i=0;i<fields.size();++i)if(fields[i]==field)vars.observed.fetch_or(uint64_t{1}<<i,std::memory_order_relaxed);
}
bool VarOriginal(void* handle,int mode,void* frame,const void* operands,void* result){
  const bool passed=reinterpret_cast<var_handle_field::Access>(original_var_access)(handle,mode,frame,operands,result);
  if(passed)++vars.original_true;else ++vars.original_false;return passed;
}
bool VarAccess(void* handle,int mode,void* frame,const void* operands,void* result){
  ++vars.entries;void* self=vars.current?vars.current():nullptr;
  if(!self){++vars.context_failures;return false;}
  // Access already owns Runnable/shared-mutator access. These named native
  // decoders/read barriers neither call JNI nor construct a JNI/thread scope.
  // Class globals track moving GC; no borrowed Class pointer is cached.
  void* klass=handle?vars.metadata.Reference(handle,0):nullptr;
  const auto instance=vars.decode_class(self,vars.instance_class);
  const auto statik=vars.decode_class(self,vars.static_class);
  if(vars.current()!=self){++vars.context_failures;return false;}
  if(!klass||!instance.reference||!statik.reference){++vars.class_failures;VarUnavailable(nullptr);return false;}
  if(klass!=reinterpret_cast<void*>(instance.reference)&&klass!=reinterpret_cast<void*>(statik.reference)){
    ++vars.other_entries; // Never read field24.
    const bool passed=VarOriginal(handle,mode,frame,operands,result);
    if(vars.current()!=self){++vars.context_failures;return false;}
    return passed;
  }
  ++vars.field_entries;
  struct Context {void* previous;Context(void* self):previous(var_access_self){var_access_self=self;}
    ~Context(){var_access_self=previous;}} context{self};
  const var_handle_field::Dispatch dispatch{&vars,VarTarget,VarRetired,VarReject,VarUnavailable,VarObserved,VarOriginal};
  const bool passed=dispatch.Invoke(handle,mode,frame,operands,result);
  if(vars.current()!=self){++vars.context_failures;return false;}
  return passed;
}
struct VarProbe {var_handle_field::Reader* reader;const FieldVarHandleOracle* oracle;bool is_static;NativeCurrent current;jfieldID field_id;bool passed=false;std::shared_ptr<contextual_field_id::Proof> identity;
  jobject actual_class;const modern_field_metadata::FieldMetadata* metadata;abi::arm64::android11::DecodeJObject decode_class;};
bool ProbeVar(const runnable_jni::Context& context,void* data){
  auto& request=*static_cast<VarProbe*>(data);void* handle=nullptr;
  if(!request.current||request.current()!=context.self)return false;
  if(!runnable_executor::DecodeStrongGlobal(context,request.oracle->strong_global,&handle))return false;
  void* klass=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,request.actual_class,&klass)||
      request.decode_class(context.self,request.actual_class).reference!=reinterpret_cast<uintptr_t>(klass)||
      request.metadata->Reference(handle,0)!=klass||request.current()!=context.self)return false;
  void* reflected=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(context,request.oracle->reflected_field,&reflected)||
     GetField(reflected)!=request.oracle->observed_field||
     contextual_field_id::NativeField(request.identity)!=request.oracle->observed_field)return false;
  request.passed=request.reader->ObserveOracle(handle,request.oracle->observed_field,request.is_static);
  return request.passed;
}
struct OtherClassProbe {jobject handle,klass,instance_class,static_class;
  const modern_field_metadata::FieldMetadata* metadata;abi::arm64::android11::DecodeJObject decode;NativeCurrent current;};
bool ProbeOtherClass(const runnable_jni::Context& context,void* opaque){
  auto& request=*static_cast<OtherClassProbe*>(opaque);void *handle=nullptr,*klass=nullptr;
  if(request.current()!=context.self||!runnable_executor::DecodeStrongGlobal(context,request.handle,&handle)||
      !runnable_executor::DecodeStrongGlobal(context,request.klass,&klass)||request.metadata->Reference(handle,0)!=klass)return false;
  const auto decoded=request.decode(context.self,request.klass);
  const auto instance=request.decode(context.self,request.instance_class),statik=request.decode(context.self,request.static_class);
  return decoded.reference==reinterpret_cast<uintptr_t>(klass)&&instance.reference&&statik.reference&&
      decoded.reference!=instance.reference&&decoded.reference!=statik.reference&&request.current()==context.self;
}
struct Entry {std::string symbol;void* proxy;void** original;void* target=nullptr;};
std::string JniSymbol(int indexed,bool is_static,bool setter,const char* type,const char* code,bool object) {
  std::string name=std::string(setter?"Set":"Get")+(is_static?"Static":"")+type+"Field";
  std::string receiver=is_static?"EP7_JNIEnvP7_jclassP9_jfieldID":"EP7_JNIEnvP8_jobjectP9_jfieldID";
  std::string argument=object?(is_static?"P8_jobject":"S5_"):code;
  const std::string owner=indexed==2?"_ZN3art12_GLOBAL__N_18CheckJNI":"_ZN3art3JNIILb"+std::to_string(indexed)+"EE";
  return owner+std::to_string(name.size())+name+receiver+(setter?argument:"");
}
std::string ReflectedSymbol(bool setter,const char* type,const char* code,bool object) {
  std::string name=std::string(setter?"Field_set":"Field_get")+(object?"":type);
  return "_ZN3artL"+std::to_string(name.size())+name+"EP7_JNIEnvP8_jobjectS3_"+(setter?(object?"S3_":code):"");
}
template<int indexed,class T,bool is_static> struct JniField {
  using Receiver=std::conditional_t<is_static,jclass,jobject>;
  static inline void *get=nullptr,*set=nullptr;
  static T Get(JNIEnv* env,Receiver receiver,jfieldID id) {
    return jni_field::GetContextual<T,Receiver>(env,receiver,id,GuardJni,
        reinterpret_cast<T(*)(JNIEnv*,Receiver,jfieldID)>(get));
  }
  static void Set(JNIEnv* env,Receiver receiver,jfieldID id,T value) {
    jni_field::SetContextual<T,Receiver>(env,receiver,id,value,GuardJni,
        reinterpret_cast<void(*)(JNIEnv*,Receiver,jfieldID,T)>(set));
  }
  static void Add(std::vector<Entry>& entries,const char* type,const char* code) {
    constexpr bool object=std::is_same_v<T,jobject>;
    entries.push_back({JniSymbol(indexed,is_static,false,type,code,object),reinterpret_cast<void*>(Get),&get});
    entries.push_back({JniSymbol(indexed,is_static,true,type,code,object),reinterpret_cast<void*>(Set),&set});
  }
};
template<class T> struct ReflectionField {
  static inline void *get=nullptr,*set=nullptr;
  static T Get(JNIEnv* env,jobject field,jobject receiver) {
    if(Guard(env,nullptr,field))return T{};
    return reinterpret_cast<T(*)(JNIEnv*,jobject,jobject)>(get)(env,field,receiver);
  }
  static void Set(JNIEnv* env,jobject field,jobject receiver,T value) {
    if(Guard(env,nullptr,field))return;
    reinterpret_cast<void(*)(JNIEnv*,jobject,jobject,T)>(set)(env,field,receiver,value);
  }
  static void Add(std::vector<Entry>& entries,const char* type,const char* code) {
    constexpr bool object=std::is_same_v<T,jobject>;
    entries.push_back({ReflectedSymbol(false,type,code,object),reinterpret_cast<void*>(Get),&get});
    entries.push_back({ReflectedSymbol(true,type,code,object),reinterpret_cast<void*>(Set),&set});
  }
};
}
bool InstallFieldEntryGuards(HookEngine& hooks,JNIEnv* env,jobject anchor,void* manager,
    const field_mirror::Calibration& mirror,RetiredFieldRegistry& registry,std::string* error) {
  if(state.registry) {
    if(installed&&state.registry==&registry&&state.manager==manager&&state.anchor==anchor)return true;
    *error="Field entry guards already owned or rollback incomplete";return false;
  }
  if(!env||!anchor||!manager||!mirror.matched||mirror.checked<2||!runnable_executor::Available()||env->ExceptionCheck()) {
    *error="Field entry guards require actual complete mirror/owned JNI calibration";return false;
  }
  using namespace abi::arm64::android_common;
  State candidate{&registry,anchor,manager,
      reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"))};
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!contextual_field_id::Initialize(hooks,error)||!candidate.throw_exception||!construct||!destroy) {
    *error="Missing named field decoder/exception/exclusive hook installation declarations";return false;
  }
  std::vector<Entry> entries{
    {"_ZN3art6mirror5Field11GetArtFieldEv",reinterpret_cast<void*>(GetField),&original_field},
    {"_ZN3art6mirror5Field11VisitTargetEPNS_22ReflectiveValueVisitorE",reinterpret_cast<void*>(VisitField),&original_visit}};
#define ADD(TYPE,T,CODE) JniField<0,T,false>::Add(entries,#TYPE,CODE); JniField<1,T,false>::Add(entries,#TYPE,CODE); \
  JniField<0,T,true>::Add(entries,#TYPE,CODE); JniField<1,T,true>::Add(entries,#TYPE,CODE); \
  JniField<2,T,false>::Add(entries,#TYPE,CODE); JniField<2,T,true>::Add(entries,#TYPE,CODE); ReflectionField<T>::Add(entries,#TYPE,CODE)
  ADD(Boolean,jboolean,"h");ADD(Byte,jbyte,"a");ADD(Char,jchar,"t");ADD(Short,jshort,"s");
  ADD(Int,jint,"i");ADD(Long,jlong,"l");ADD(Float,jfloat,"f");ADD(Double,jdouble,"d");ADD(Object,jobject,"");
#undef ADD
  for(auto& entry:entries) {
    entry.target=hooks.FindSymbol("libart.so",entry.symbol.c_str());
    if(!entry.target){*error="Missing field guard entry: "+entry.symbol;return false;}
  }
  SuspendAll stopped{};construct(&stopped,"artdcevm field guard installation",false);
  struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
  state=candidate;
  std::vector<void*> tokens;
  for(auto& entry:entries) {
    void* token=hooks.Install(entry.target,entry.proxy,entry.original,error);
    if(!token) {
      bool removed=true;
      for(auto it=tokens.rbegin();it!=tokens.rend();++it) {
        std::string rollback;
        if(!hooks.Remove(*it,&rollback)){removed=false;*error+="; "+rollback;}
      }
      if(removed)state={};
      return false;
    }
    tokens.push_back(token);
  }
  installed=true;return true;
}
bool FieldEntryGuardsOwn(const RetiredFieldRegistry& registry){return installed&&state.registry==&registry;}
bool RejectRetiredField(void* field,void* self,void* result){return GuardNativeField(self,field,result);}
bool InstallMethodHandleFieldGuards(HookEngine&,std::string* error){
 if(!installed){if(error)*error="MethodHandle field guards require the durable field entry owner";return false;}
 return RegisterMethodHandleFieldOwner(*state.registry,error);
}
bool InstallFieldVarHandleGuards(HookEngine& hooks,JNIEnv* env,
    const std::vector<FieldVarHandleOracle>& oracles,RetiredFieldRegistry& registry,std::string* error){
  if(vars_installed&&vars.registry!=&registry){*error="VarHandle guards already owned by another registry";return false;}
  if(!installed||state.registry!=&registry||!env||env->ExceptionCheck()||
      oracles.size()<2||oracles.size()>63){*error="VarHandle guards require complete same field owner and independent rooted oracles";return false;}
  var_handle_field::Declarations declarations;
  if(!declarations.Resolve(hooks)){*error="Missing native VarHandle Access/current/exclusive installation declarations";return false;}
  auto target=declarations.access;
  auto current=reinterpret_cast<NativeCurrent>(declarations.current);
  auto visit=reinterpret_cast<var_handle_field::Visit>(declarations.visit);
  if(vars_installed&&(vars.access_target!=target||vars.current!=current||
      !vars.reader||vars.reader->Visitor()!=visit||reinterpret_cast<void*>(vars.decode_class)!=declarations.decode)) {
    *error="VarHandle named declarations disagree with the durable owner";return false;
  }
  using namespace abi::arm64::android_common;
  auto construct=reinterpret_cast<SuspendConstruct>(declarations.construct);
  auto destroy=reinterpret_cast<SuspendDestroy>(declarations.destroy);
  jclass base=env->FindClass("java/lang/invoke/FieldVarHandle");
  jclass field=base?env->FindClass("java/lang/reflect/Field"):nullptr;
  jclass list=field?env->FindClass("java/util/List"):nullptr;
  struct Classes {JNIEnv* env;jclass base,field,list;~Classes(){
    if(list)env->DeleteLocalRef(list);if(field)env->DeleteLocalRef(field);if(base)env->DeleteLocalRef(base);
  }} classes{env,base,field,list};
  if(!base||!field||!list||env->ExceptionCheck()){
    *error="Missing actual field VarHandle or public reflection declarations";return false;}
  const auto method=[&](jclass cls,const char* name,const char* signature){
    return env->ExceptionCheck()?nullptr:env->GetMethodID(cls,name,signature);
  };
  jmethodID coordinates=method(base,"coordinateTypes","()Ljava/util/List;");
  jmethodID size=method(list,"size","()I");
  jmethodID get=method(list,"get","(I)Ljava/lang/Object;");
  jmethodID modifiers=method(field,"getModifiers","()I");
  jmethodID declaring=method(field,"getDeclaringClass","()Ljava/lang/Class;");
  if(!coordinates||!size||!get||!modifiers||!declaring||env->ExceptionCheck()){
    *error="Missing public field VarHandle coordinate oracle";return false;}
  modern_field_metadata::FieldMetadata metadata;
  if(!metadata.Bind(hooks,error))return false;
  const auto decode_class=reinterpret_cast<abi::arm64::android11::DecodeJObject>(declarations.decode);
  struct Roots {JNIEnv* env;std::vector<jobject> values;~Roots(){for(auto value:values)if(value)env->DeleteGlobalRef(value);}
    jobject Add(jobject value){jobject root=value?env->NewGlobalRef(value):nullptr;if(root)values.push_back(root);return root;}
    void Keep(jobject value){for(auto& root:values)if(root==value){root=nullptr;return;}}} roots{env,{}};
  jobject instance_class=nullptr,static_class=nullptr;
  auto reader=std::make_unique<var_handle_field::Reader>(visit);
  for(const auto& oracle:oracles){
    if(!oracle.strong_global||!oracle.observed_field||!oracle.reflected_field||
       !env->IsInstanceOf(oracle.strong_global,base)||!env->IsInstanceOf(oracle.reflected_field,field)||env->ExceptionCheck()){
      *error="VarHandle oracle requires a rooted ART FieldVarHandle and independent reflected Field";return false;}
    jfieldID id=env->FromReflectedField(oracle.reflected_field);
    if(!id||env->ExceptionCheck()){*error="Invalid independent reflected field ID";return false;}
    const jint flags=env->CallIntMethod(oracle.reflected_field,modifiers);
    if(env->ExceptionCheck()){*error="Independent reflected field modifiers failed";return false;}
    jobject types=env->CallObjectMethod(oracle.strong_global,coordinates);
    struct Local {JNIEnv* env;jobject value;~Local(){if(value)env->DeleteLocalRef(value);}} local{env,types};
    if(!id||!types||env->ExceptionCheck()||!env->IsInstanceOf(types,list)){
      *error="Invalid public field VarHandle coordinate oracle";return false;}
    const jint count=env->CallIntMethod(types,size);
    const bool is_static=(flags&0x8)!=0;
    if(env->ExceptionCheck()||count!=(is_static?0:1)){
      *error="VarHandle coordinates disagree with independent field static modifiers";return false;}
    if(!is_static){
      jobject coordinate=env->CallObjectMethod(types,get,0);
      Local coordinate_local{env,coordinate};
      if(env->ExceptionCheck()){*error="VarHandle coordinate lookup failed";return false;}
      jobject owner=env->CallObjectMethod(oracle.reflected_field,declaring);
      Local owner_local{env,owner};
      if(!coordinate||!owner||env->ExceptionCheck()||!env->IsSameObject(coordinate,owner)){
        *error="VarHandle coordinate owner disagrees with independent reflected Field";return false;}
    }
    auto identity=contextual_field_id::Prepare(env,oracle.reflected_field,id,error);
    if(!identity)return false;
    struct Identity {JNIEnv* env;std::shared_ptr<contextual_field_id::Proof>& value;~Identity(){contextual_field_id::Release(env,&value);}} identity_release{env,identity};
    jclass actual=env->GetObjectClass(oracle.strong_global);
    Local actual_local{env,actual};
    jobject class_root=roots.Add(actual);
    if(!class_root||env->ExceptionCheck()){
      *error="VarHandle class proof requires actual independently rooted public Class";return false;
    }
    jobject& role=is_static?static_class:instance_class;
    if(role&&!env->IsSameObject(role,class_root)){
      *error="VarHandle oracles disagree on exact instance/static runtime Class";return false;
    }
    if(!role)role=class_root;
    VarProbe request{reader.get(),&oracle,is_static,current,id,false,identity,class_root,&metadata,decode_class};
    if(runnable_executor::With(env,oracle.strong_global,ProbeVar,&request)!=runnable_jni::Result::kSuccess||!request.passed){
      *error="Native VarHandle target disagrees with independent field oracle";return false;}
  }
  if(!reader->Seal()||!instance_class||!static_class||env->ExceptionCheck()){
    *error="VarHandle target proof requires distinct fields and independently proven instance/static Classes";return false;
  }
  if(vars_installed&&(!env->IsSameObject(instance_class,vars.instance_class)||!env->IsSameObject(static_class,vars.static_class)||env->ExceptionCheck())){
    *error="VarHandle public Classes disagree with the durable native owner";return false;
  }
  // Third independent public Class oracle establishes a non-field path. All
  // lookups/allocation/JNI occur here in Native, never inside the hot hook.
  jclass lookup=env->FindClass("com/github/ytlog/artdcevm/internal/VarHandleClassOracle");
  struct Locals {JNIEnv* env;jclass lookup;jobject handle=nullptr,klass=nullptr;~Locals(){
    if(klass)env->DeleteLocalRef(klass);if(handle)env->DeleteLocalRef(handle);if(lookup)env->DeleteLocalRef(lookup);}} local{env,lookup};
  jmethodID factory=lookup?env->GetStaticMethodID(lookup,"create","()Ljava/lang/invoke/VarHandle;"):nullptr;
  local.handle=factory?env->CallStaticObjectMethod(lookup,factory):nullptr;
  local.klass=local.handle&&!env->ExceptionCheck()?env->GetObjectClass(local.handle):nullptr;
  jobject other=roots.Add(local.handle),other_class=roots.Add(local.klass);
  if(!other||!other_class||env->ExceptionCheck()||env->IsInstanceOf(other,base)||
      env->IsSameObject(other_class,instance_class)||env->IsSameObject(other_class,static_class)||env->ExceptionCheck()){
    *error="Public non-field VarHandle Class oracle unavailable or overlaps field Classes";return false;
  }
  OtherClassProbe other_probe{other,other_class,instance_class,static_class,&metadata,decode_class,current};
  if(runnable_executor::With(env,other,ProbeOtherClass,&other_probe)!=runnable_jni::Result::kSuccess){
    *error="Native VarHandle Class reader disagrees with independent public non-field Class";return false;
  }
  SuspendAll stopped{};construct(&stopped,"artdcevm VarHandle field guard installation",false);
  struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
  // Every preparation proves the current rooted instance/static pair readonly.
  // Publish a fresh proof epoch only after all public/JNI/native checks pass.
  // The suspended durable owner keeps its sole original Access trampoline.
  if(!vars_installed) {
    void* token=hooks.Install(target,reinterpret_cast<void*>(VarAccess),&original_var_access,error);
    if(!token)return false;
    if(!original_var_access)std::abort();
  }
  vars.reader=std::move(reader);vars.registry=&registry;vars.current=current;vars.access_target=target;
  vars.complete=(uint64_t{1}<<oracles.size())-1;vars.observed.store(0);
  vars.metadata=metadata;vars.decode_class=decode_class;
  // Same boot Classes keep their existing process roots; new preparation roots
  // remain temporary. Never replace/release a live root under SuspendAll.
  if(!vars_installed){vars.instance_class=instance_class;vars.static_class=static_class;roots.Keep(instance_class);roots.Keep(static_class);}
  vars.entries.store(0);vars.field_entries.store(0);vars.other_entries.store(0);vars.target_seen.store(0);
  vars.original_true.store(0);vars.original_false.store(0);vars.context_failures.store(0);vars.class_failures.store(0);
  ++vars.generation;vars_installed=true;return true;
}
FieldVarHandleGuardDiagnostics InspectFieldVarHandleGuards(const RetiredFieldRegistry& registry){
  return {vars_installed,vars.registry==&registry,vars.reader&&vars.reader->Prepared(),
      vars.reader&&vars.reader->UsesNamedVisitor(),vars.complete,vars.observed.load(std::memory_order_relaxed),vars.generation,
      vars.entries.load(),vars.field_entries.load(),vars.other_entries.load(),vars.target_seen.load(),
      vars.original_true.load(),vars.original_false.load(),vars.context_failures.load(),vars.class_failures.load()};
}
std::string DescribeFieldVarHandleGuards(const RetiredFieldRegistry& registry){
  const auto proof=InspectFieldVarHandleGuards(registry);
  return "VH(installed="+std::to_string(proof.installed)+",owner="+std::to_string(proof.owner_matches)+
      ",reader="+std::to_string(proof.reader_prepared)+",named="+std::to_string(proof.named_visitor)+
      ",generation="+std::to_string(proof.oracle_generation)+",expected="+std::to_string(proof.expected_mask)+
      ",observed="+std::to_string(proof.observed_mask)+",entries="+std::to_string(proof.entries)+
      ",fieldEntries="+std::to_string(proof.field_entries)+",otherEntries="+std::to_string(proof.other_entries)+
      ",targetSeen="+std::to_string(proof.target_seen_mask)+",originalTrue="+std::to_string(proof.original_true)+
      ",originalFalse="+std::to_string(proof.original_false)+",contextFailures="+std::to_string(proof.context_failures)+
      ",classFailures="+std::to_string(proof.class_failures)+")";
}
bool FieldVarHandleGuardsReady(const RetiredFieldRegistry& registry){
  const auto proof=InspectFieldVarHandleGuards(registry);
  return proof.installed&&proof.owner_matches&&proof.reader_prepared&&proof.expected_mask&&
      proof.observed_mask==proof.expected_mask;
}

bool MethodHandleFieldGuardsInstalled(const RetiredFieldRegistry& registry){return MethodHandleFieldGuardsReady(registry);}
size_t RejectedFieldEntries(){return rejected.load();}
}  // namespace artdcevm::retirement
