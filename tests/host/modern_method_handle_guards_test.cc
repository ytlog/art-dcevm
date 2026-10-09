#include "platform/common/modern_method_handle_guards.cc"
#include "platform/common/modern_final_field_guard.h"
#include <cassert>
#include <iostream>
namespace artdcevm::contextual_field_id {
struct Proof {void* native;jfieldID id;};
void* NativeField(const std::shared_ptr<Proof>& p){return p?p->native:nullptr;}
jfieldID Id(const std::shared_ptr<Proof>& p){return p?p->id:nullptr;}
}
namespace art {class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;uint32_t type=3,pad=0;void* object=nullptr;};}
using namespace artdcevm;
namespace {
retirement::RetiredMethodRegistry method_registry,wrong_method_registry;
retirement::RetiredFieldRegistry field_registry,wrong_field_registry;
void* current=reinterpret_cast<void*>(0x700010);bool stopped=false,normal_result=true,wrong_owner=false;
size_t invoked=0,installed_count=0,private_lookups=0;uint32_t source_kind=3;int exception_kind=0;
std::string missing;void* expected_self=current;bool duplicate_visit=false;
uint32_t override_kind=UINT32_MAX;bool final_targets=false;
void* Object(size_t n){return reinterpret_cast<void*>(0x10000+n*16);}
void* Identity(void* object){return reinterpret_cast<void*>(uintptr_t(object)+uintptr_t{0x7000000000});}
uint32_t Kind(const void* object){if(override_kind!=UINT32_MAX)return override_kind;
 return ((uintptr_t(object)-0x10000)/16)%2==0?8:10;}
bool IsField(void* object){return uintptr_t(object)>=uintptr_t(Object(4));}
void Visit(void* object,art::ReflectiveValueVisitor* visitor){art::ReflectionSourceInfo source;source.type=source_kind;source.object=wrong_owner?nullptr:object;
 if(IsField(object)){auto* field=reinterpret_cast<art::ArtField*>(Identity(object));assert(visitor->VisitField(field,source)==field);if(duplicate_visit)visitor->VisitField(field,source);}
 else {auto* method=reinterpret_cast<art::ArtMethod*>(Identity(object));assert(visitor->VisitMethod(method,source)==method);if(duplicate_visit)visitor->VisitMethod(method,source);}}
size_t direct_field_calls=0;
void* DirectField(jfieldID id){++direct_field_calls;return reinterpret_cast<void*>(uintptr_t(id)+0x100000u);}
void* DecodeMethod(void*,jmethodID id){return reinterpret_cast<void*>(id);}void* DecodeField(void*,jfieldID id){return reinterpret_cast<void*>(id);}
void* ReadRoot(const void* slot){uint32_t value;std::memcpy(&value,slot,4);return reinterpret_cast<void*>(uintptr_t(value));}
void* Current(){return current;}
bool Normal(void* self,void* frame,retirement::Handle handle,retirement::Handle type,const void* operands,void* result){
 assert(self==expected_self&&frame==reinterpret_cast<void*>(7)&&handle.slot&&type.slot==reinterpret_cast<void*>(11)&&operands==reinterpret_cast<void*>(9)&&result);
 ++invoked;*static_cast<uint64_t*>(result)=0x1234;if(!normal_result)exception_kind=7;return normal_result;}
void Stop(void*,const char*,bool){assert(!stopped);stopped=true;}void Resume(void*){assert(stopped);stopped=false;}
struct Hooks:HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* symbol)override{const std::string s=symbol;
  if(s.find("DoMethodHandleInvokeMethod")!=std::string::npos||s.find("RefineTargetMethod")!=std::string::npos||s.find("MethodHandleFieldGet")!=std::string::npos||s.find("MethodHandleFieldPut")!=std::string::npos){++private_lookups;return nullptr;}
  if(s==missing)return nullptr;
  if(s=="_ZN3art6mirror12MethodHandle11VisitTargetEPNS_22ReflectiveValueVisitorE")return reinterpret_cast<void*>(Visit);
  if(s=="_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID")return reinterpret_cast<void*>(DecodeMethod);
  if(s=="_ZN3art22DecodeArtFieldInternalEP9_jfieldID")return reinterpret_cast<void*>(DirectField);
  if(s=="_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID")return reinterpret_cast<void*>(DecodeField);
  if(s=="artReadBarrierForRootSlow")return reinterpret_cast<void*>(ReadRoot);
  if(s=="_ZN3art6Thread14CurrentFromGdbEv")return reinterpret_cast<void*>(Current);
  // Independent fixed external source/ELF catalog, never internal private ABI.
  if(s=="_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE"||
     s=="_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE")return reinterpret_cast<void*>(Normal);
  if(s==abi::arm64::android_common::kSuspendConstruct)return reinterpret_cast<void*>(Stop);
  if(s==abi::arm64::android_common::kSuspendDestroy)return reinterpret_cast<void*>(Resume);return nullptr;}
 void* Install(void* target,void*,void** original,std::string*)override{assert(stopped);*original=target;++installed_count;return this;}bool Remove(void*,std::string*)override{return true;}};
}
namespace artdcevm::runnable_executor {
bool DecodeStrongGlobal(const runnable_jni::Context&,jobject object,void** out){*out=reinterpret_cast<void*>(object);return object;}
runnable_jni::Result With(JNIEnv* env,jobject,runnable_jni::Operation op,void* data){return op({current,env,nullptr},data)?runnable_jni::Result::kSuccess:runnable_jni::Result::kOperationRejected;}
}
namespace artdcevm::retirement {
bool MethodEntryGuardsOwn(const RetiredMethodRegistry& owner){return &owner==&method_registry;}
bool FieldEntryGuardsOwn(const RetiredFieldRegistry& owner){return &owner==&field_registry;}
bool RejectRetiredMethod(void* target,void*,void* result){if(!method_registry.Contains(target))return false;exception_kind=1;if(result)std::memset(result,0,8);return true;}
bool RejectFinalFieldSetter(void*,void*,void* result){
 final_field_guard::Access access;access.path=final_field_guard::Path::MethodHandleSetter;
 access.field_flags=final_targets?0x10:0;
 if(final_field_guard::Check(access)==final_field_guard::Decision::Delegate)return false;
 exception_kind=3;if(result)std::memset(result,0,8);return true;
}
bool RejectRetiredField(void* target,void*,void* result){if(!field_registry.TokenFor(target))return false;exception_kind=2;if(result)std::memset(result,0,8);return true;}
}
int main(){using namespace retirement;Hooks hooks;std::string error;JNIEnv* env=reinterpret_cast<JNIEnv*>(1);void* manager=reinterpret_cast<void*>(3);
 // Production TU selector reads fixed source prefixes, not SDK or model data.
 // Both getters and setters participate; a changed/bogus/ambiguous source
 // remains rejected rather than silently choosing the newest declaration.
 for(uint32_t kind=8;kind<=11;++kind){
  abi::arm64::android11::MethodHandlePrefix old{};old.kind=kind+1;
  abi::arm64::android_common::MethodHandlePrefix recent{};recent.kind=kind;recent.method_type=0x1234;
  assert(KindCandidates(&old,kind,3)==1);
  assert(KindCandidates(&recent,kind,3)==2);
  assert(KindCandidates(&old,kind==11?8:kind+1,1)==0);
  assert(KindCandidates(&old,kind,2)==0); // Previously bound source cannot switch.
  old.kind=0;assert(KindCandidates(&old,kind,3)==0); // Unknown source kind is unproven.
 }
 std::vector<MethodHandleOracle> methods;for(size_t i=1;i<=3;++i)methods.push_back({reinterpret_cast<jobject>(Object(i)),reinterpret_cast<jmethodID>(Identity(Object(i)))});
 std::vector<FieldMethodHandleOracle> fields;for(size_t i=4;i<=5;++i)fields.push_back({reinterpret_cast<jobject>(Object(i)),reinterpret_cast<jfieldID>(uintptr_t(Identity(Object(i)))-0x100000u),std::make_shared<contextual_field_id::Proof>(contextual_field_id::Proof{Identity(Object(i)),reinterpret_cast<jfieldID>(uintptr_t(Identity(Object(i)))-0x100000u)})});
 assert(!PrepareMethodHandleGuards(hooks,env,manager,wrong_method_registry,methods,&error));
 source_kind=4;assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,methods,&error));source_kind=3;
 wrong_owner=true;assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,methods,&error));wrong_owner=false;
 duplicate_visit=true;assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,methods,&error));duplicate_visit=false;
 auto bad_methods=methods;bad_methods[1]=bad_methods[0];assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,bad_methods,&error));
 auto bad_fields=fields;bad_fields[1]=bad_fields[0];assert(!PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,bad_fields,&error));
 bad_fields=fields;bad_fields[0].field=fields[1].field;assert(!PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,bad_fields,&error));
 for(const auto* symbol:{"artReadBarrierForRootSlow",kInvoke,kExact}){missing=symbol;assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,methods,&error));}missing.clear();assert(installed_count==0);
 assert(PrepareMethodHandleGuards(hooks,env,manager,method_registry,methods,&error));assert(installed_count==2&&!stopped&&!MethodHandleGuardsReady(method_registry));
 assert(RegisterMethodHandleFieldOwner(field_registry,&error));assert(!RegisterMethodHandleFieldOwner(wrong_field_registry,&error));
 assert(PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,fields,&error));assert(installed_count==2&&!MethodHandleFieldGuardsReady(field_registry));
 // Actual readonly production calibration checks independent construction kind
 // alongside the existing native visitor/JNI target identity, without changing
 // previously valid retirement state when a kind oracle fails.
 Binding kind_binding=state.binding;kind_binding.kind=Kind;
 auto kind_oracles=fields;kind_oracles[0].expected_kind=8;kind_oracles[1].expected_kind=10;
 Calibration kind_request{&kind_binding,manager,nullptr,&kind_oracles,{},false,{}};
 assert(Calibrate({current,env,nullptr},&kind_request)&&kind_request.field_kinds_calibrated);
 kind_oracles[1].expected_kind=9;Calibration wrong_kind{&kind_binding,manager,nullptr,&kind_oracles,{},false,{}};
 assert(!Calibrate({current,env,nullptr},&wrong_kind));kind_oracles[1].expected_kind=10;
 auto unknown_kinds=kind_oracles;unknown_kinds[0].expected_kind=UINT32_MAX;
 Calibration unknown_kind{&kind_binding,manager,nullptr,&unknown_kinds,{},false,{}};
 assert(Calibrate({current,env,nullptr},&unknown_kind)&&!unknown_kind.field_kinds_calibrated);
 uint64_t result=99;uint32_t root=uint32_t(uintptr_t(Object(1)));auto call=[&](bool exact){return exact?Dispatch<true>(expected_self,reinterpret_cast<void*>(7),{&root},{reinterpret_cast<void*>(11)},reinterpret_cast<void*>(9),&result):Dispatch<false>(expected_self,reinterpret_cast<void*>(7),{&root},{reinterpret_cast<void*>(11)},reinterpret_cast<void*>(9),&result);};
 assert(BeginMethodHandleTargetProof(env,methods[0].strong_global,methods[0].method,&error));assert(call(true)&&result==0x1234);assert(EndMethodHandleTargetProof(true,&error)&&MethodHandleGuardsReady(method_registry));
 normal_result=false;exception_kind=0;assert(!call(false)&&exception_kind==7&&result==0x1234);normal_result=true;
 assert(BeginMethodHandleTargetProof(env,methods[0].strong_global,methods[0].method,&error));expected_self=reinterpret_cast<void*>(0x700020);assert(call(false));assert(!EndMethodHandleTargetProof(true,&error));expected_self=current;
 root=uint32_t(uintptr_t(Object(4)));normal_result=false;assert(BeginMethodHandleFieldTargetProof(env,fields[0].strong_global,fields[0].field,&error));assert(!call(false));assert(!EndMethodHandleFieldTargetProof(true,&error)&&!MethodHandleFieldGuardsReady(field_registry));normal_result=true;
 for(size_t i=0;i<fields.size();++i){root=uint32_t(uintptr_t(Object(i+4)));assert(BeginMethodHandleFieldTargetProof(env,fields[i].strong_global,fields[i].field,&error));assert(call(i==0));assert(EndMethodHandleFieldTargetProof(true,&error));assert(MethodHandleFieldGuardsReady(field_registry)==(i==1));}
 // A second class contributes two different targets to the same durable owner.
 // Preparation must recheck them, reset proof range, and never reinstall hooks.
 std::vector<FieldMethodHandleOracle> next_fields;
 for(size_t i=6;i<=7;++i)next_fields.push_back({reinterpret_cast<jobject>(Object(i)),reinterpret_cast<jfieldID>(uintptr_t(Identity(Object(i)))-0x100000u),std::make_shared<contextual_field_id::Proof>(contextual_field_id::Proof{Identity(Object(i)),reinterpret_cast<jfieldID>(uintptr_t(Identity(Object(i)))-0x100000u)})});
 const auto previous_targets=state.field_targets;const auto previous_mask=state.field_observed.load();const auto previous_batch=state.field_batch;
 bad_fields=next_fields;bad_fields[1]=bad_fields[0];
 assert(!PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,bad_fields,&error));
 bad_fields=next_fields;bad_fields[0].field=next_fields[1].field;
 assert(!PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,bad_fields,&error));
 assert(state.field_targets==previous_targets&&state.field_observed.load()==previous_mask&&state.field_batch==previous_batch&&MethodHandleFieldGuardsReady(field_registry)&&installed_count==2);
 bad_methods=methods;bad_methods[1]=bad_methods[0];
 assert(!PrepareMethodHandleGuards(hooks,env,manager,method_registry,bad_methods,&error)&&MethodHandleGuardsReady(method_registry));
 assert(PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,next_fields,&error));
 assert(installed_count==2&&state.field_targets!=previous_targets&&state.field_batch==previous_batch+1&&!MethodHandleFieldGuardsReady(field_registry));
 // An original call can hit a former valid target, but must not authorize the
 // new class's batch. This was the real standalone -> FieldSubject failure.
 root=uint32_t(uintptr_t(Object(4)));assert(!BeginMethodHandleFieldTargetProof(env,fields[0].strong_global,fields[0].field,&error));assert(call(true));assert(!EndMethodHandleFieldTargetProof(true,&error)&&!MethodHandleFieldGuardsReady(field_registry));
 for(size_t i=0;i<next_fields.size();++i){root=uint32_t(uintptr_t(Object(i+6)));
  assert(BeginMethodHandleFieldTargetProof(env,next_fields[i].strong_global,next_fields[i].field,&error));
  assert(!PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,fields,&error));
  assert(call(i==0));assert(EndMethodHandleFieldTargetProof(true,&error));
  assert(MethodHandleFieldGuardsReady(field_registry)==(i==1));
 }
 // Independent Java value failure revokes that target's previous proof.
 assert(BeginMethodHandleFieldTargetProof(env,next_fields[1].strong_global,next_fields[1].field,&error));assert(!MethodHandleFieldGuardsReady(field_registry));assert(call(false));assert(!EndMethodHandleFieldTargetProof(false,&error)&&!MethodHandleFieldGuardsReady(field_registry));
 assert(BeginMethodHandleFieldTargetProof(env,next_fields[1].strong_global,next_fields[1].field,&error));assert(call(false));assert(EndMethodHandleFieldTargetProof(true,&error)&&MethodHandleFieldGuardsReady(field_registry));
 // Exercise both actual exported outer dispatches after source-kind proof.
 // Current final flags reject cached setters before the original native call;
 // getter delegation and removal of final remain valid.
 state.field_kinds_calibrated=true;
 assert(PrepareMethodHandleFieldGuards(hooks,env,manager,field_registry,next_fields,&error));
 assert(state.field_kinds_calibrated); // Later unknown target oracles cannot revoke installed enforcement.
 state.binding.kind=Kind;
 root=uint32_t(uintptr_t(Object(6)));final_targets=true;override_kind=9;result=99;
 const auto before_final=invoked;
 assert(!call(false)&&exception_kind==3&&result==0&&invoked==before_final);
 root=uint32_t(uintptr_t(Object(7)));override_kind=11;result=99;
 assert(!call(true)&&exception_kind==3&&result==0&&invoked==before_final);
 override_kind=10;assert(call(true)&&result==0x1234&&invoked==before_final+1);
 final_targets=false;override_kind=11;assert(call(false)&&result==0x1234&&invoked==before_final+2);
 override_kind=UINT32_MAX;
 root=uint32_t(uintptr_t(Object(1)));method_registry.Retire(reinterpret_cast<void*>(methods[0].method));const auto before=invoked;result=99;assert(!call(true)&&exception_kind==1&&result==0&&invoked==before);
 root=uint32_t(uintptr_t(Object(4)));field_registry.Retire(Identity(Object(4)));result=99;assert(!call(false)&&exception_kind==2&&result==0&&invoked==before);
 assert(private_lookups==0&&installed_count==2&&!MethodHandleGuardsReady(wrong_method_registry)&&!MethodHandleFieldGuardsReady(wrong_field_registry));
 assert(direct_field_calls==0);
 std::cout<<"Unique exported MethodHandle owner: 3 method/4 field independent readonly oracles across two batches; 6-argument exact/invoke forwarding, true original proofs, false/exception preservation and shared retirement rejection passed\n";
}
