#include "platform/common/contextual_field_id.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/common/modern_method_guards.h"
#include "platform/common/modern_field_guards.h"
#include "platform/abi/arm64/android-common/method_handle.h"
#include "platform/abi/arm64/android11/method_handle.h"
#include "platform/common/native_structural_reflection_visitor.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/arm64/android-common/suspend_all.h"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
namespace artdcevm::retirement {
namespace {
using Visit=void(*)(void*,art::ReflectiveValueVisitor*);
using DecodeMethod=void*(*)(void*,jmethodID);

using Root=void*(*)(const void*);
// Fixed public method_handles.h and trivially copied Handle<T> declaration.
// Only exported MethodHandleInvoke/InvokeExact are hooked. Internal LLVM
// callees can eliminate arguments without changing their ELF symbol name.
struct Handle {const void* slot;};
using Invoke=bool(*)(void*,void*,Handle,Handle,const void*,void*);
constexpr const char* kInvoke="_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE";
constexpr const char* kExact="_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE";
struct Binding {Visit visit=nullptr;Root root=nullptr;void*(*current)()=nullptr;
 DecodeMethod method=nullptr;uint32_t(*kind)(const void*)=nullptr;void* targets[2]{};
 abi::arm64::android_common::SuspendConstruct construct=nullptr;
 abi::arm64::android_common::SuspendDestroy destroy=nullptr;};
struct State {Binding binding;void* manager=nullptr;RetiredMethodRegistry* methods=nullptr;
 RetiredFieldRegistry* fields=nullptr;bool methods_calibrated=false,fields_calibrated=false;
 bool installed=false,incomplete=false,method_observed=false,field_kinds_calibrated=false;Invoke originals[2]{};
 uint64_t field_batch=0;
 std::vector<FieldMethodHandleOracle> field_oracles;
 std::vector<void*> field_targets;std::atomic<uint64_t> field_observed{0};};
State state;
struct Proof {void* self=nullptr;void* target=nullptr;size_t hits=0;bool field=false;uint64_t batch=0;};
thread_local Proof proof;
struct Member {void* value=nullptr;bool field=false,valid=false;const char* error=nullptr;uint32_t source_kind=0;size_t callbacks=0;};
class Target final:public art::ReflectiveValueVisitor {
 public:
 explicit Target(void* object):object_(object){}
 art::ArtMethod* VisitMethod(art::ArtMethod* value,const art::ReflectionSourceInfo& info)override{Observe(value,false,info);return value;}
 art::ArtField* VisitField(art::ArtField* value,const art::ReflectionSourceInfo& info)override{Observe(value,true,info);return value;}
 Member Result()const{return {value_,field_,count_==1&&!invalid_&&value_,error_,source_kind_,count_};}
 private:
 void Observe(void* value,bool field,const art::ReflectionSourceInfo& info){
  void* owner=nullptr;std::memcpy(&owner,reinterpret_cast<const char*>(&info)+16,8);
  ++count_;source_kind_=native_structural_reflection::detail::Type(info);
  if(source_kind_!=3){invalid_=true;error_="reflective source kind differs from MethodHandle";}
  else if(owner!=object_){invalid_=true;error_="reflective source owner differs from rooted MethodHandle";}
  else if(count_!=1){invalid_=true;error_="multiple native targets in MethodHandle visitor";}
  else if(!value){invalid_=true;error_="null native MethodHandle target";}
  value_=value;field_=field;
 }
 void* object_;void* value_=nullptr;bool field_=false,invalid_=false;size_t count_=0;uint32_t source_kind_=0;const char* error_="native visitor did not supply a target";
};
Member TargetOf(const Binding& b,void* object){if(!object||!b.visit)return {};Target visitor(object);b.visit(object,&visitor);return visitor.Result();}
template<bool exact> bool Dispatch(void* self,void* frame,Handle handle,Handle type,const void* operands,void* result){
 if(!state.installed||state.incomplete||!state.originals[exact?1:0])std::abort();
 void* object=handle.slot?state.binding.root(handle.slot):nullptr;
 const auto target=TargetOf(state.binding,object);
 if(!target.valid)std::abort(); // Calibrated source visitor invariants, no offset scanning.
 if(target.field){
  if(state.fields_calibrated){
   if(!FieldEntryGuardsOwn(*state.fields))std::abort();
   if(RejectRetiredField(target.value,self,result))return false;
   if(state.field_kinds_calibrated){
    using namespace abi::arm64::android_common;
    const auto kind=state.binding.kind(object);
    if((kind==kInstancePut||kind==kStaticPut)&&RejectFinalFieldSetter(target.value,self,result))return false;
   }
  }
 }else if(state.methods_calibrated){
  if(!MethodEntryGuardsOwn(*state.methods))std::abort();
  if(RejectRetiredMethod(target.value,self,result))return false;
 }
 // Forward every exported ABI argument unchanged, including both root slots.
 const bool success=state.originals[exact?1:0](self,frame,handle,type,operands,result);
 if(success&&self==state.binding.current()){
  if(proof.self==self&&proof.target==target.value&&proof.field==target.field)++proof.hits;
 }
 return success; // Original false/pending exception remains unchanged.
}
bool Resolve(HookEngine& hooks,Binding* out,std::string* error){
 Binding b;
 b.kind=state.field_kinds_calibrated?state.binding.kind:nullptr;
 b.visit=reinterpret_cast<Visit>(hooks.FindSymbol("libart.so","_ZN3art6mirror12MethodHandle11VisitTargetEPNS_22ReflectiveValueVisitorE"));
 b.root=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 b.current=reinterpret_cast<void*(*)()>(hooks.FindSymbol("libart.so","_ZN3art6Thread14CurrentFromGdbEv"));
 b.method=reinterpret_cast<DecodeMethod>(hooks.FindSymbol("libart.so","_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"));
 b.targets[0]=hooks.FindSymbol("libart.so",kInvoke);b.targets[1]=hooks.FindSymbol("libart.so",kExact);
 using namespace abi::arm64::android_common;
 b.construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
 b.destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
 if(!b.visit||!b.root||!b.current||!b.method||!b.targets[0]||!b.targets[1]||!b.construct||!b.destroy){if(error)*error="Missing stable exported MethodHandle outer/visitor/root declarations";return false;}
 *out=b;return true;
}
bool Install(HookEngine& hooks,const Binding& b,std::string* error){
 if(state.incomplete){if(error)*error="Incomplete outer MethodHandle rollback";return false;}
 if(state.installed)return true;
 state.binding=b;
 using namespace abi::arm64::android_common;
 SuspendAll stopped{};b.construct(&stopped,"artdcevm shared outer MethodHandle owner",false);
 void* tokens[2]{};
 tokens[0]=hooks.Install(b.targets[0],reinterpret_cast<void*>(Dispatch<false>),reinterpret_cast<void**>(&state.originals[0]),error);
 if(tokens[0])tokens[1]=hooks.Install(b.targets[1],reinterpret_cast<void*>(Dispatch<true>),reinterpret_cast<void**>(&state.originals[1]),error);
 if(!tokens[0]||!tokens[1]){
  if(tokens[0]){std::string rollback;if(!hooks.Remove(tokens[0],&rollback)){state.incomplete=true;if(error)*error+="; "+rollback;}}
  if(!state.incomplete){state.originals[0]=state.originals[1]=nullptr;state.binding={};}
  b.destroy(&stopped);return false;
 }
 if(!state.originals[0]||!state.originals[1])std::abort();
 state.installed=true;b.destroy(&stopped);return true;
}
struct Calibration {const Binding* binding;void* manager;const std::vector<MethodHandleOracle>* methods=nullptr;
 const std::vector<FieldMethodHandleOracle>* fields=nullptr;std::vector<void*> targets;bool valid=false;std::string error;bool field_kinds_calibrated=false;uint32_t(*kind)(const void*)=nullptr;};
// Intersect only fixed, source-declared layouts; never scan object storage.
unsigned KindCandidates(const void* object,uint32_t expected,unsigned candidates){
 if(abi::arm64::android11::MethodHandleKind(object)!=expected)candidates&=~1u;
 if(abi::arm64::android_common::MethodHandleKind(object)!=expected)candidates&=~2u;
 return candidates;
}
bool Calibrate(const runnable_jni::Context& context,void* raw){
 auto& request=*static_cast<Calibration*>(raw);const auto& b=*request.binding;
 if(b.current()!=context.self){request.error="actual Thread differs from Runnable context";return false;}std::unordered_set<void*> seen;
 const auto observe=[&](jobject global,void* expected,bool field,jfieldID field_id=nullptr){void* object=nullptr;
  if(!global||!expected){request.error="missing root or independently decoded JNI target";return false;}
  if(!runnable_executor::DecodeStrongGlobal(context,global,&object)){request.error="strong MethodHandle root decoding failed";return false;}
  const auto target=TargetOf(b,object);
  if(!target.valid){request.error=std::string(target.error?target.error:"invalid native MethodHandle visitor")+" (source kind="+std::to_string(target.source_kind)+", callbacks="+std::to_string(target.callbacks)+")";return false;}
  if(target.field!=field){request.error="MethodHandle target family differs from independent JNI oracle";return false;}
  if(target.value!=expected){request.error="native MethodHandle target differs from independently decoded JNI ID"+(" [id="+std::to_string(uintptr_t(field_id))+", native="+std::to_string(uintptr_t(expected))+", handle="+std::to_string(uintptr_t(target.value))+"]");return false;}
  if(!seen.insert(expected).second){request.error="duplicate independent native target";return false;}
  if(uintptr_t(object)>UINT32_MAX){request.error="managed MethodHandle exceeds fixed compressed-root declaration";return false;}
  const uint32_t root=uint32_t(uintptr_t(object));if(b.root(&root)!=object){request.error="named root barrier disagrees with independent strong root";return false;}
  request.targets.push_back(expected);return true;
 };
 if(request.methods)for(const auto& o:*request.methods){if(!o.method||!observe(o.strong_global,b.method(request.manager,o.method),false))return false;}
 if(request.fields){
  bool kinds=true;unsigned candidates=3;
  for(const auto& o:*request.fields){
   if(!o.identity||contextual_field_id::Id(o.identity)!=o.field||!o.field||!observe(o.strong_global,contextual_field_id::NativeField(o.identity),true,o.field))return false;
   if(o.expected_kind==UINT32_MAX){kinds=false;continue;}
   void* object=nullptr;
   if(o.expected_kind<8||o.expected_kind>11||
      !runnable_executor::DecodeStrongGlobal(context,o.strong_global,&object)){
     request.error="Invalid independent field construction kind/root oracle";return false;
   }
   if(b.kind){
    if(b.kind(object)!=o.expected_kind){request.error="MethodHandle kind differs from independent field construction oracle";return false;}
   }else{
    candidates=KindCandidates(object,o.expected_kind,candidates);
    if(!candidates){request.error="MethodHandle kind differs from every fixed source construction oracle";return false;}
   }
  }
  if(kinds){
   if(!b.kind&&candidates!=1&&candidates!=2){request.error="MethodHandle source layout is absent or ambiguous under independent kind oracles";return false;}
   request.kind=b.kind?b.kind:(candidates==1?abi::arm64::android11::MethodHandleKind:abi::arm64::android_common::MethodHandleKind);
  }
  request.field_kinds_calibrated=kinds;
 }
 request.valid=true;return true;
}
struct Begin {jmethodID method=nullptr;jfieldID field=nullptr;bool valid=false;jobject root=nullptr;};
bool Capture(const runnable_jni::Context& context,void* raw){auto& request=*static_cast<Begin*>(raw);
 if(context.self!=state.binding.current()||proof.self)return false;
 void* target=nullptr;
 if(request.field){void* object=nullptr;if(!request.root||!runnable_executor::DecodeStrongGlobal(context,request.root,&object))return false;
  const auto observed=TargetOf(state.binding,object);if(!observed.valid||!observed.field)return false;
  for(const auto& oracle:state.field_oracles)if(oracle.field==request.field&&contextual_field_id::NativeField(oracle.identity)==observed.value){target=observed.value;break;}
 }else target=state.binding.method(state.manager,request.method);if(!target)return false;
 proof={context.self,target,0,request.field!=nullptr,state.field_batch};request.valid=true;return true;
}
}
bool RegisterMethodHandleFieldOwner(RetiredFieldRegistry& registry,std::string* error){
 if(!FieldEntryGuardsOwn(registry)||(state.fields&&state.fields!=&registry)){if(error)*error="MethodHandle field owner differs from installed entry owner";return false;}
 state.fields=&registry;return true;
}
bool PrepareMethodHandleGuards(HookEngine& hooks,JNIEnv* env,void* manager,
 RetiredMethodRegistry& registry,const std::vector<MethodHandleOracle>& oracles,std::string* error){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!manager||oracles.size()<3||!MethodEntryGuardsOwn(registry)||
    (state.methods&&state.methods!=&registry)||(state.manager&&state.manager!=manager))return fail("MethodHandle requires the same installed method owner and complete independent oracles");
 if(proof.self)return fail("Cannot prepare while a MethodHandle invocation proof is active");
 Binding b;if(!Resolve(hooks,&b,error))return false;
 Calibration request{&b,manager,&oracles,nullptr,{},false,{}};
 if(runnable_executor::With(env,oracles.front().strong_global,Calibrate,&request)!=runnable_jni::Result::kSuccess||!request.valid)return fail(request.error.empty()?"Independent MethodHandle target/root oracle mismatch":request.error.c_str());
 if(!Install(hooks,b,error))return false;
 state.manager=manager;state.methods=&registry;state.methods_calibrated=true;state.method_observed=false;return true;
}
bool PrepareMethodHandleFieldGuards(HookEngine& hooks,JNIEnv* env,void* manager,
 RetiredFieldRegistry& registry,const std::vector<FieldMethodHandleOracle>& oracles,std::string* error){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!manager||oracles.size()<2||oracles.size()>63||!FieldEntryGuardsOwn(registry)||
    (state.fields&&state.fields!=&registry)||(state.manager&&state.manager!=manager))return fail("MethodHandle field proof requires the same entry owner and independent targets");
 if(proof.self)return fail("Cannot replace field batch while a MethodHandle invocation proof is active");
 Binding b;if(!Resolve(hooks,&b,error))return false;
 Calibration request{&b,manager,nullptr,&oracles,{},false,{}};
 if(runnable_executor::With(env,oracles.front().strong_global,Calibrate,&request)!=runnable_jni::Result::kSuccess||!request.valid)return fail(request.error.empty()?"Independent field MethodHandle target/root oracle mismatch":request.error.c_str());
 if(request.field_kinds_calibrated)b.kind=request.kind;
 if(!Install(hooks,b,error))return false;
 if(request.field_kinds_calibrated)state.binding.kind=request.kind;
 state.manager=manager;state.fields=&registry;state.field_targets=std::move(request.targets);state.field_oracles=oracles;state.field_observed.store(0);++state.field_batch;state.fields_calibrated=true;state.field_kinds_calibrated=state.field_kinds_calibrated||request.field_kinds_calibrated;return true;
}
bool BeginMethodHandleTargetProof(JNIEnv* env,jobject root,jmethodID method,std::string* error){
 if(!state.methods_calibrated||proof.self||!env||!root||!method){if(error)*error="Unprepared/active MethodHandle target proof";return false;}
 Begin request{method,nullptr,false};const bool valid=runnable_executor::With(env,root,Capture,&request)==runnable_jni::Result::kSuccess&&request.valid;
 if(!valid&&error)*error="MethodHandle proof lacks actual current Thread/JNI identity";return valid;
}
bool EndMethodHandleTargetProof(bool invocation_succeeded,std::string* error){const bool valid=invocation_succeeded&&proof.self&&proof.hits>0&&!proof.field;proof={};
 if(valid)state.method_observed=true;else if(error)*error="Actual owned MethodHandle call missed calibrated outer dispatch";return valid;}
bool BeginMethodHandleFieldTargetProof(JNIEnv* env,jobject root,jfieldID field,std::string* error){
 if(!state.fields_calibrated||proof.self||!env||!root||!field){if(error)*error="Unprepared/active field MethodHandle target proof";return false;}
 Begin request{nullptr,field,false,root};const bool valid=runnable_executor::With(env,root,Capture,&request)==runnable_jni::Result::kSuccess&&request.valid;
 if(valid)for(size_t i=0;i<state.field_targets.size();++i)if(state.field_targets[i]==proof.target)
  state.field_observed.fetch_and(~(uint64_t{1}<<i),std::memory_order_relaxed);
 if(!valid&&error)*error="Field MethodHandle proof lacks actual Thread/JNI identity";return valid;
}
bool EndMethodHandleFieldTargetProof(bool invocation_succeeded,std::string* error){
 const bool valid=invocation_succeeded&&proof.self&&proof.hits>0&&proof.field&&proof.batch==state.field_batch;
 bool matched=false;
 if(valid)for(size_t i=0;i<state.field_targets.size();++i)if(state.field_targets[i]==proof.target){
  state.field_observed.fetch_or(uint64_t{1}<<i,std::memory_order_relaxed);matched=true;
 }
 if(!matched&&error){
  if(!invocation_succeeded)*error="Independent Java field invocation result failed";
  else if(proof.batch!=state.field_batch)*error="Field MethodHandle proof belongs to a replaced oracle batch";
  else if(!proof.hits)*error="Actual original field invocation missed current Thread/native target";
  else *error="Field MethodHandle target is absent from current calibrated batch";
 }
 proof={};return matched;
}
bool MethodHandleGuardsReady(const RetiredMethodRegistry& registry){return state.installed&&state.methods_calibrated&&state.method_observed&&state.methods==&registry&&MethodEntryGuardsOwn(registry);}
bool MethodHandleFieldGuardsReady(const RetiredFieldRegistry& registry){
 return state.installed&&state.fields_calibrated&&state.fields==&registry&&FieldEntryGuardsOwn(registry)&&
  !state.field_targets.empty()&&state.field_observed.load(std::memory_order_relaxed)==((uint64_t{1}<<state.field_targets.size())-1);
}
}
