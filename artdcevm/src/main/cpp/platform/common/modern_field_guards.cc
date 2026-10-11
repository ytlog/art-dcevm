#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_field_guards.h"
#include "platform/common/native_receiver_guard.h"
#include "platform/common/native_receiver_original_ingress.h"
#include "platform/common/modern_final_field_guard.h"
#include "platform/abi/arm64/art_field.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/var_handle_field_target.h"
#include "platform/common/var_handle_access_mode.h"
#include "platform/common/modern_field_metadata.h"
#include <memory>
#include "platform/common/jni_field_dispatch.h"
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
struct Request {jfieldID id=nullptr;jobject mirror=nullptr;bool guarded=false;jobject receiver=nullptr;bool write=false;};
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
  if(!request.guarded){
    const auto path=request.write?receiver_guard::Path::ReflectWrite:receiver_guard::Path::ReflectRead;
    if(receiver_guard::OriginalIngress::WantsBridge(path)){
      const auto*actual=runnable_jni::Bridge::BorrowOriginalScope(context);
      bool unavailable=false;
      request.guarded=actual?receiver_guard::OriginalIngress::Check(*actual,field,request.receiver,path,&unavailable,true):true;
      if(!actual||unavailable)state.throw_exception(context.self,"Ljava/lang/IllegalStateException;","Receiver reflected field lost actual original Runnable scope");
    }else request.guarded=receiver_guard::CheckReference(path,context.self,field,request.receiver,nullptr,true);
  }
  return true;
}
bool Guard(JNIEnv* env,jfieldID id,jobject reflected,jobject receiver=nullptr,bool write=false) {
  if(probing||(state.registry->Count()==0&&!receiver_guard::InUse()))return false;
  if(env->ExceptionCheck())return true; // Keep the caller's existing exception.
  struct Probe {Probe(){probing=true;}~Probe(){probing=false;}} probe;
  jobject global=reflected?env->NewGlobalRef(reflected):nullptr;
  jobject receiver_root=receiver?env->NewGlobalRef(receiver):nullptr;
  Request request{id,global,false,receiver_root,write};
  const auto path=write?receiver_guard::Path::ReflectWrite:receiver_guard::Path::ReflectRead;
  auto result=(reflected&&!global)||(receiver&&!receiver_root)?runnable_jni::Result::kInvalidRequest:
      (receiver_guard::OriginalIngress::WantsBridge(path)?runnable_executor::WithOriginalScope(env,global?global:state.anchor,Observe,&request):
       runnable_executor::With(env,global?global:state.anchor,Observe,&request));
  if(global)env->DeleteGlobalRef(global);
  if(receiver_root)env->DeleteGlobalRef(receiver_root);
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
bool GuardReceiverJni(JNIEnv* env,jobject receiver,jfieldID id,bool write){
 if(!receiver_guard::InUse())return false;
 if(env->ExceptionCheck())return true;
 jobject root=receiver?env->NewGlobalRef(receiver):nullptr;
 struct Probe{jobject receiver;jfieldID id;bool write;bool rejected=false;}request{root,id,write};
 const auto operation=[](const runnable_jni::Context&context,void*p){auto&r=*static_cast<Probe*>(p);const auto path=r.write?receiver_guard::Path::JniWrite:receiver_guard::Path::JniRead;
  if(receiver_guard::OriginalIngress::WantsBridge(path)){
   const auto*actual=runnable_jni::Bridge::BorrowOriginalScope(context);bool unavailable=false;
   r.rejected=actual?receiver_guard::OriginalIngress::CheckFieldId(*actual,r.id,r.receiver,path,&unavailable):true;
   if(!actual||unavailable)state.throw_exception(context.self,"Ljava/lang/IllegalStateException;","Receiver JNI field lost actual original Runnable scope");
  }else r.rejected=receiver_guard::CheckFieldId(path,context.self,r.id,r.receiver);
  return true;};
 const auto path=write?receiver_guard::Path::JniWrite:receiver_guard::Path::JniRead;
 const auto result=receiver&&!root?runnable_jni::Result::kInvalidRequest:
  (receiver_guard::OriginalIngress::WantsBridge(path)?runnable_executor::WithOriginalScope(env,state.anchor,operation,&request):runnable_executor::With(env,state.anchor,operation,&request));
 if(root)env->DeleteGlobalRef(root);
 if(result!=runnable_jni::Result::kSuccess&&!env->ExceptionCheck()){auto type=env->FindClass("java/lang/IllegalStateException");if(type){env->ThrowNew(type,"Receiver JNI field guard lost owned Runnable access");env->DeleteLocalRef(type);}}
 return request.rejected||result!=runnable_jni::Result::kSuccess;
}
bool GuardJni(JNIEnv* env,jobject receiver,bool is_static,jfieldID id,bool write=false){
 if(probing)return false;
 if(state.registry->Count()==0)return !is_static&&GuardReceiverJni(env,receiver,id,write);
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
 if(!state.registry->TokenFor(field))return !is_static&&GuardReceiverJni(env,receiver,id,write);
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
using ModeTemplate=var_handle_access_mode::NativeTemplate;
ModeTemplate mode_template=var_handle_access_mode::Template;
bool ReadFinalFlags(void*,void* field,uint32_t* flags){
  if(!installed||!field||!flags)return false;
  // InstallFieldEntryGuards requires a complete independent mirror/native field
  // oracle, which validates this shared fixed ArtField scalar declaration.
  *flags=__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(field)+
      offsetof(abi::arm64::Field,access_flags)),__ATOMIC_RELAXED);return true;
}
void RaiseFinal(void*,void* self,const char* type,const char* text){state.throw_exception(self,type,text);}

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
  // Retired-member rejection retains precedence. A cached VarHandle's access
  // mask can predate a mutable-to-final evolution, so the original mask alone
  // does not enforce current field permissions.
  void* field=vars.reader->Target(handle);
  if(field&&!vars.registry->TokenFor(field)){
    final_field_guard::Access access;access.path=final_field_guard::Path::VarHandle;
    access.var_mode_valid=var_handle_access_mode::Template(mode)!=var_handle_access_mode::kInvalid;
    access.var_mode_template=access.var_mode_valid&&mode_template?mode_template(mode):var_handle_access_mode::kInvalid;
    const final_field_guard::Boundary boundary{&state,ReadFinalFlags,RaiseFinal};
    if(final_field_guard::Reject(boundary,field,self,access,result))return false;
    bool unavailable=false;
    if(receiver_guard::OriginalIngress::CheckVarHandle(self,field,frame,
       static_cast<const receiver_guard::InstructionOperands*>(operands),result,&unavailable)){
      if(unavailable)VarUnavailable(nullptr);
      return false;
    }
  }
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
    return jni_field::GetContextual<T,Receiver>(env,receiver,id,[](JNIEnv* e,jobject r,bool s,jfieldID f){return GuardJni(e,r,s,f,false);},
        reinterpret_cast<T(*)(JNIEnv*,Receiver,jfieldID)>(get));
  }
  static void Set(JNIEnv* env,Receiver receiver,jfieldID id,T value) {
    jni_field::SetContextual<T,Receiver>(env,receiver,id,value,[](JNIEnv* e,jobject r,bool s,jfieldID f){return GuardJni(e,r,s,f,true);},
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
    if(Guard(env,nullptr,field,receiver,false))return T{};
    return reinterpret_cast<T(*)(JNIEnv*,jobject,jobject)>(get)(env,field,receiver);
  }
  static void Set(JNIEnv* env,jobject field,jobject receiver,T value) {
    if(Guard(env,nullptr,field,receiver,true))return;
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
  std::string ignored;if(!error)error=&ignored;
  if(!env||!anchor||!manager||!mirror.matched||mirror.checked<2||!runnable_executor::Available()||env->ExceptionCheck()||
     env->GetObjectRefType(anchor)!=JNIGlobalRefType||env->ExceptionCheck()) {
    *error="Field entry guards require actual complete mirror/owned JNI calibration";return false;
  }
  if(state.registry) {
    if(installed&&state.registry==&registry&&state.manager==manager&&state.anchor==anchor)return true;
    *error="Field entry guards already owned or rollback incomplete";return false;
  }
  State candidate{&registry,anchor,manager,
      reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"))};
  exclusive_owned_roots::Service exclusive;
  if(!contextual_field_id::Initialize(hooks,error)||!candidate.throw_exception||
     !exclusive_owned_roots::Service::Bind(hooks,&exclusive,error)) {
    *error="Missing named field decoder/exception/exclusive hook installation declarations";return false;
  }
  ModeTemplate candidate_mode=nullptr;
  const auto native_mode=reinterpret_cast<ModeTemplate>(hooks.FindSymbol(
      "libart.so",var_handle_access_mode::kNamedTemplate));
  if(!var_handle_access_mode::Bind(native_mode,&candidate_mode)) {
    *error="Native VarHandle access modes disagree with fixed source declaration";return false;
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
  // Reserve the rollback journal before entering the actual exclusive window.
  // Publish the durable owner only after Native/Runnable and root calibration.
  struct Install {
    HookEngine& hooks;State candidate;std::vector<Entry>& entries;
    std::vector<void*> tokens;ModeTemplate mode;std::string* error;bool completed=false;
  }request{hooks,candidate,entries,{},candidate_mode,error};
  request.tokens.reserve(entries.size());
  const jobject roots[]{anchor};
  const bool stopped=exclusive.With(env,roots,1,[](const exclusive_owned_roots::Context& actual,void*raw){
    auto&r=*static_cast<Install*>(raw);
    if(!actual.self||actual.count!=1||!actual.roots||!actual.roots[0])return false;
    state=r.candidate;
    try{
      for(auto&entry:r.entries){
        void*token=r.hooks.Install(entry.target,entry.proxy,entry.original,r.error);
        if(!token)break;
        r.tokens.push_back(token);
      }
    }catch(...){
      // Roll back below while the original exclusive stop is still owned.
    }
    if(r.tokens.size()!=r.entries.size()){
      bool removed=true;
      for(auto it=r.tokens.rbegin();it!=r.tokens.rend();++it){
        try{std::string rollback;if(!r.hooks.Remove(*it,&rollback))removed=false;}
        catch(...){removed=false;}
      }
      if(removed)state={}; // Otherwise retain the durable owner and refuse retry.
      return false;
    }
    mode_template=r.mode;installed=true;r.completed=true;return true;
  },&request,error);
  return stopped&&request.completed&&FieldEntryGuardsOwn(registry)&&!env->ExceptionCheck();
}
bool FieldEntryGuardsOwn(const RetiredFieldRegistry& registry){return installed&&state.registry==&registry;}
bool FieldEntryGuardsOwn(const RetiredFieldRegistry& registry,const void*manager){
 return manager&&FieldEntryGuardsOwn(registry)&&state.manager==manager;
}
bool RejectRetiredField(void* field,void* self,void* result){return GuardNativeField(self,field,result);}
bool RejectFinalFieldSetter(void* field,void* self,void* result){
  const final_field_guard::Boundary boundary{&state,ReadFinalFlags,RaiseFinal};
  final_field_guard::Access access;access.path=final_field_guard::Path::MethodHandleSetter;
  return final_field_guard::Reject(boundary,field,self,access,result);
}
bool InstallMethodHandleFieldGuards(HookEngine&,std::string* error){
 if(!installed){if(error)*error="MethodHandle field guards require the durable field entry owner";return false;}
 return RegisterMethodHandleFieldOwner(*state.registry,error);
}
bool InstallFieldVarHandleGuards(HookEngine& hooks,JNIEnv* env,
    const std::vector<FieldVarHandleOracle>& oracles,RetiredFieldRegistry& registry,std::string* error){
  std::string ignored;if(!error)error=&ignored;
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
  exclusive_owned_roots::Service exclusive;
  if(!exclusive_owned_roots::Service::Bind(hooks,&exclusive,error))return false;
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
    jobject Add(jobject value){
      jobject root=value?env->NewGlobalRef(value):nullptr;
      if(root){try{values.push_back(root);}catch(...){env->DeleteGlobalRef(root);throw;}}return root;}
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
  // Recheck Native/Runnable ownership and reload all public Class roots in the
  // shared exclusive service. No decoded Class crosses this possible GC boundary.
  bool published=false;
  auto install=[&](const exclusive_owned_roots::Context&actual){
    if(!actual.self||actual.self!=current()||actual.count!=3||!actual.roots||
       !actual.roots[0]||!actual.roots[1]||!actual.roots[2])return false;
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
    // remain temporary. No JNI root is released in the exclusive callback.
    if(!vars_installed){vars.instance_class=instance_class;vars.static_class=static_class;roots.Keep(instance_class);roots.Keep(static_class);}
    vars.entries.store(0);vars.field_entries.store(0);vars.other_entries.store(0);vars.target_seen.store(0);
    vars.original_true.store(0);vars.original_false.store(0);vars.context_failures.store(0);vars.class_failures.store(0);
    ++vars.generation;vars_installed=true;published=true;return true;
  };
  const jobject binding_roots[]{instance_class,static_class,other_class};
  const bool stopped=exclusive.With(env,binding_roots,3,
    [](const exclusive_owned_roots::Context&actual,void*raw){
      return (*static_cast<decltype(install)*>(raw))(actual);
    },&install,error);
  return stopped&&published&&!env->ExceptionCheck();
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
