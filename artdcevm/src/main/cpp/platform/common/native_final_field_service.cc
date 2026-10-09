#include "platform/common/native_final_field_service.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/native_final_field_aliases.h"
#include "platform/common/redefinition_gate.h"
#include "platform/common/modern_method_handle_guards.h"
#include "platform/android13/schema_snapshot.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <mutex>
namespace artdcevm::final_field_service {
namespace {
void Breakpoint(jvmtiEnv*,JNIEnv*,jthread,jmethodID,jlocation){}
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool SameMember(const evolution::Member& a,const evolution::Member& b){return a.name==b.name&&a.descriptor==b.descriptor&&native_schema_policy::JavaFlags(a.access)==native_schema_policy::JavaFlags(b.access);}
bool Members(const std::vector<evolution::Member>& a,const std::vector<evolution::Member>& b){return a.size()==b.size()&&std::all_of(a.begin(),a.end(),[&](const auto& m){return std::count_if(b.begin(),b.end(),[&](const auto& n){return SameMember(m,n);})==1;});}
bool Schema(const evolution::ClassSchema& a,const evolution::ClassSchema& b){return a.descriptor==b.descriptor&&a.superclass==b.superclass&&a.interfaces==b.interfaces&&native_schema_policy::JavaFlags(a.access)==native_schema_policy::JavaFlags(b.access)&&Members(a.fields,b.fields)&&Members(a.methods,b.methods);}
const evolution::Member* Member(const evolution::ClassSchema& schema,const evolution::Member& field){
 auto it=std::find_if(schema.fields.begin(),schema.fields.end(),[&](const auto& f){return f.name==field.name&&f.descriptor==field.descriptor&&f.IsStatic()==field.IsStatic();});return it==schema.fields.end()?nullptr:&*it;
}
bool Handles(const Bindings& bindings){return bindings.registry&&retirement::FieldEntryGuardsOwn(*bindings.registry)&&
 retirement::MethodHandleFieldGuardsReady(*bindings.registry)&&retirement::FieldVarHandleGuardsReady(*bindings.registry);}
bool NeedsGuard(const evolution::Member& old,const evolution::Member& next){return (next.access&0x10u)&&((old.access^next.access)&0x17u);}
void* Field(const modern_field_metadata::FieldMetadata& metadata,void* klass,const evolution::Member& member,std::string* error){
 field_array::View fields;if(!metadata.Fields(klass,member.IsStatic(),&fields,error))return nullptr;
 void* result=nullptr;
 for(uint32_t i=0;i<fields.count;++i)if(fields.Selected(i)){field_copy::CopyFieldView field;void* native=fields.At(i);
  if(!metadata.FieldIdentity(native,&field,error))return nullptr;
  if(field.name&&field.descriptor&&member.name==field.name&&member.descriptor==field.descriptor){if(result)return nullptr;result=native;}}
 return result;
}
}
struct Service::State {
 struct Public {jclass root=nullptr,replacement=nullptr;jweak weak=nullptr;evolution::ClassSchema schema;};
 using Node=final_field_aliases::Node;
 struct Pair {size_t index=0;size_t public_index=0;void* old_anchor=nullptr;void* new_anchor=nullptr;evolution::ClassSchema before,after;};
 explicit State(Bindings b):bindings(b),guard({this,ReadField,MethodOwner,Equivalent,Raise,EquivalentWriter}),aliases({this,nullptr,OldOwner,DecodeWeak,OldOwner,ReadMethodOwner}){}
 uint8_t proved_storage=0;bool current_static=false,calibration_complete=false;
 Bindings bindings;jvmtiEnv* permission_env=nullptr;jmethodID proof_method=nullptr;final_write_guard::Guard guard;std::vector<Public> staged;
 source_commit::Plan expected;std::vector<size_t> rooted_pairs;std::vector<Pair> pairs;std::unique_ptr<Node> pending;
 final_field_aliases::Set aliases;bool prepared=false,sealed=false,committed=false,needs_access=false;
 static bool ReadField(void* p,void* field,uint32_t* flags,void** owner){auto& s=*static_cast<State*>(p);
  if(!s.bindings.native.metadata||!s.bindings.native.metadata->Calibrated()||!field)return false;
  *flags=field_array::Flags(field);*owner=s.bindings.native.metadata->FieldDeclaringClass(field);return *owner!=nullptr;}
 static bool MethodOwner(void* p,void* method,void** owner){auto& s=*static_cast<State*>(p);if(!s.bindings.native.method_class)return false;*owner=s.bindings.native.method_class(s.bindings.native.owner,method);return *owner!=nullptr;}
 static void Raise(void* p,void* thread,const char* type,const char* text){auto& s=*static_cast<State*>(p);s.bindings.raise(s.bindings.native.owner,thread,type,text);}
 static bool DecodeWeak(void* p,void* thread,void* weak,void** current){auto& s=*static_cast<State*>(p);return s.bindings.decode_weak(s.bindings.native.owner,thread,static_cast<jweak>(weak),current);}
 static void* OldOwner(void* p,void* field){auto& s=*static_cast<State*>(p);return s.bindings.native.metadata->FieldDeclaringClass(field);}
 static void* ReadMethodOwner(void* p,void* method){auto& s=*static_cast<State*>(p);return s.bindings.native.method_class(s.bindings.native.owner,method);}
 static bool EquivalentWriter(void* p,void* thread,void* field,void* method){return static_cast<State*>(p)->aliases.EquivalentWriter(thread,field,method);}
 static bool Equivalent(void* p,void* field_owner,void* method_owner){return static_cast<State*>(p)->aliases.Equivalent(field_owner,method_owner);}

};
Service::Service(Bindings bindings):state_(std::make_unique<State>(bindings)){}
Service::~Service()=default;
bool Service::PrepareEnvironment(JNIEnv* env,std::string* error){auto& s=*state_;
 if(s.permission_env)return true;
 if(!env||env->ExceptionCheck()||!s.bindings.native.vm)return Fail(error,"Final-field permission environment lacks its process VM owner");
 JavaVM* actual=nullptr;jvmtiEnv* owned=nullptr;
 if(env->GetJavaVM(&actual)!=JNI_OK||actual!=s.bindings.native.vm||
    actual->GetEnv(reinterpret_cast<void**>(&owned),JVMTI_VERSION_1_2)!=JNI_OK||!owned)
  return Fail(error,"Final-field dedicated JVMTI environment unavailable");
 jvmtiCapabilities capabilities{};capabilities.can_generate_breakpoint_events=1;
 jvmtiEventCallbacks callbacks{};callbacks.Breakpoint=Breakpoint;
 if(owned->AddCapabilities(&capabilities)!=JVMTI_ERROR_NONE||
    owned->SetEventCallbacks(&callbacks,sizeof(callbacks))!=JVMTI_ERROR_NONE||
    owned->SetEventNotificationMode(JVMTI_ENABLE,JVMTI_EVENT_BREAKPOINT,nullptr)!=JVMTI_ERROR_NONE){
  owned->DisposeEnvironment();return Fail(error,"Final-field dedicated deoptimization lease could not activate");
 }
 // Enabling breakpoint notifications alone does not globally deoptimize. The
 // isolated SetBreakpoint below owns the actual deoptimization requester.
 s.permission_env=owned;return true;
}
bool Service::PrepareProofMethod(jmethodID method,std::string* error){auto& s=*state_;
 if(!s.permission_env||!method)return Fail(error,"Final-field proof lacks its owned JVMTI environment/fixture method");
 if(s.proof_method)return s.proof_method==method||Fail(error,"Final-field proof environment already owns another fixture");
 // Fixed source breakpoint handling deoptimizes only the selected method and
 // owns a requester until the breakpoint/environment is retired. Never clear
 // this isolated breakpoint while evolved permission consumers remain alive.
 if(s.permission_env->SetBreakpoint(method,0)!=JVMTI_ERROR_NONE)
  return Fail(error,"Final-field isolated fixture breakpoint could not activate deoptimization lease");
 s.proof_method=method;return true;
}
bool Service::Bind(HookEngine& hooks,void* instrumentation,std::string* error){auto& s=*state_;
 if(!s.permission_env||!s.proof_method||!s.bindings.decode_weak||!s.bindings.raise||!s.bindings.native.metadata||!s.bindings.native.metadata->Calibrated()||
    !s.bindings.native.methods||!s.bindings.native.decode_global||!s.bindings.native.method_class)
  return Fail(error,"Final-field service lacks the calibrated process identity/root owner");
 return final_write_guard::BindListener(hooks,instrumentation,s.guard,error);
}
bool Service::InstallAtExclusiveBoundary(std::string* error){return final_write_guard::InstallListenerAtExclusiveBoundary(error);}
bool Service::FixtureField(void* thread,jclass target,jclass writer,jmethodID method,jfieldID id,bool statik,final_write_guard::Oracle* oracle,uint32_t* flags,std::string* error)const{const auto& s=*state_;
 if(!thread||!target||!writer||!method||!id||!oracle||!flags||!s.bindings.native.decode_field||!s.bindings.native.decode_method||!s.bindings.native.decode_global)
  return Fail(error,"Final proof fixture lacks independently calibrated public/native identities");
 void *klass=nullptr,*foreign=nullptr;
 if(!s.bindings.native.decode_global(s.bindings.native.owner,target,&klass)||!s.bindings.native.decode_global(s.bindings.native.owner,writer,&foreign)||!klass||!foreign||klass==foreign)return false;
 const auto target_view=s.bindings.native.metadata->Class(klass,false),writer_view=s.bindings.native.metadata->Class(foreign,false);
 if(!target_view.descriptor||!writer_view.descriptor||std::string(target_view.descriptor)!="Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle$Target;"||std::string(writer_view.descriptor)!="Lcom/github/ytlog/artdcevm/internal/FinalPermissionOracle;")return Fail(error,"Final permission Class descriptors differ from installed owned fixtures");
 void* field=s.bindings.native.decode_field(s.bindings.native.owner,target,id);
 void* actual=s.bindings.native.decode_method(s.bindings.native.owner,method);
 field_copy::CopyFieldView view;
 if(!field||!actual||!s.bindings.native.metadata->FieldIdentity(field,&view,error)||!view.name||!view.descriptor||
    std::string(view.name)!=(statik?"staticValue":"instanceValue")||std::string(view.descriptor)!="I"||
    s.bindings.native.metadata->FieldDeclaringClass(field)!=klass||s.bindings.native.method_class(s.bindings.native.owner,actual)!=foreign||
    !s.bindings.native.writable||!s.bindings.native.writable(s.bindings.native.owner,field,sizeof(field_array::Field)))return Fail(error,"Final proof public IDs disagree with exact writable fixture declarations");
 field_array::View array;uint32_t index=0;
 if(!s.bindings.native.metadata->Fields(klass,statik,&array,error)||!array.IndexOf(field,&index)||((field_array::Flags(field)&8u)!=0)!=statik)return false;
 *oracle={thread,actual,field};*flags=field_array::Flags(field);return true;
}
bool Service::SetFixtureFlags(const final_write_guard::Oracle& oracle,uint32_t expected,uint32_t desired,std::string* error){auto& s=*state_;
 if(!redefinition_gate::Owned(s.bindings.native.vm)||!oracle.field||!s.bindings.native.writable||!s.bindings.native.writable(s.bindings.native.owner,oracle.field,sizeof(field_array::Field))||
    field_array::Flags(oracle.field)!=expected||(expected^desired)!=0x10u)return Fail(error,"Final fixture mutation lost exact flags/writable native field proof");
 // Fixed ART ArtField access_flags_ is an ordinary uint32_t at declaration +4.
 // This API is only consumed by the source-owned exclusive probe boundary.
 std::memcpy(static_cast<unsigned char*>(oracle.field)+4,&desired,sizeof(desired));return true;
}
bool Service::BeginProof(const final_write_guard::Oracle& oracle){auto& s=*state_;
 if(!s.proof_method||!s.bindings.native.decode_method||s.bindings.native.decode_method(s.bindings.native.owner,s.proof_method)!=oracle.method||!s.guard.BeginProof(oracle))return false;
 s.current_static=(field_array::Flags(oracle.field)&8u)!=0;return true;
}
bool Service::EndProof(bool exception,bool unchanged){auto& s=*state_;bool pass=s.guard.EndProof(exception,unchanged);if(pass)s.proved_storage|=s.current_static?2u:1u;return pass;}
bool Service::AlreadyCalibrated()const{return state_->calibration_complete&&final_write_guard::ListenerProven(state_->guard);}
void Service::AbortCalibration(){auto& s=*state_;if(s.calibration_complete)return;s.guard.ResetUncommittedProof();s.proved_storage=0;}
bool Service::CompleteCalibration(){auto& s=*state_;s.calibration_complete=s.proved_storage==3u&&final_write_guard::ListenerProven(s.guard);return s.calibration_complete;}
bool Service::Stage(JNIEnv* env,jvmtiEnv* jvmti,const std::vector<jclass>& roots,const std::vector<jclass>& replacements,std::string* error){auto& s=*state_;
 if(roots.size()!=replacements.size()||!env||!jvmti||s.prepared||s.sealed||!s.staged.empty()||!redefinition_gate::Owned(s.bindings.native.vm))return Fail(error,"Final-field public stage lacks its operation lease");
 for(size_t i=0;i<roots.size();++i){auto klass=roots[i];if(!klass)return Fail(error,"Final-field stage contains a null public Class");State::Public entry;
  if(android13::ReadLiveSchema(jvmti,env,klass,&entry.schema,error)!=JVMTI_ERROR_NONE)return false;
  entry.root=static_cast<jclass>(env->NewGlobalRef(klass));if(!entry.root||env->ExceptionCheck())return false;
  entry.weak=env->NewWeakGlobalRef(klass);if(!entry.weak||env->ExceptionCheck()){env->DeleteGlobalRef(entry.root);return false;}
  entry.replacement=static_cast<jclass>(env->NewGlobalRef(replacements[i]));
  if(!entry.replacement||env->ExceptionCheck()){env->DeleteGlobalRef(entry.root);env->DeleteWeakGlobalRef(entry.weak);return false;}
  s.staged.push_back(std::move(entry));
 }
 return true;
}
bool Service::PreparePermissions(const source_commit::Plan& plan,std::string* error){auto& s=*state_;
 if(s.prepared||s.sealed||!redefinition_gate::Owned(s.bindings.native.vm)||plan.old_classes.size()!=plan.new_classes.size()||
    plan.old_classes.empty())return Fail(error,"Final-field seal lacks exact paired source Classes");
 if(!s.bindings.native.metadata||!s.bindings.native.methods||!s.bindings.native.decode_global)return Fail(error,"Permission owner lacks calibrated native declaration access");
 std::vector<size_t> rooted;
 for(size_t i=0;i<plan.old_classes.size();++i){size_t found=s.staged.size();
  for(size_t n=0;n<s.staged.size();++n){void *old=nullptr,*next=nullptr;
   if(!s.bindings.native.decode_global(s.bindings.native.owner,s.staged[n].root,&old)||!s.bindings.native.decode_global(s.bindings.native.owner,s.staged[n].replacement,&next))return false;
   if(old==plan.old_classes[i]&&next==plan.new_classes[i]){if(found!=s.staged.size())return Fail(error,"Duplicate independently rooted Class pair");found=n;}
  }
  if(found==s.staged.size())return Fail(error,"Permission Plan lacks independently owned paired public Class roots");
  rooted.push_back(found);
 }
 auto pending=std::make_unique<State::Node>();std::vector<State::Pair> pairs;
 for(size_t i=0;i<plan.old_classes.size();++i){evolution::ClassSchema before,after;
  if(!native_schema_policy::ReadNativeDeclarations(*s.bindings.native.metadata,*s.bindings.native.methods,plan.old_classes[i],&before,error)||
     !native_schema_policy::ReadNativeDeclarations(*s.bindings.native.metadata,*s.bindings.native.methods,plan.new_classes[i],&after,error))return false;
  std::vector<const evolution::Member*> changed;
  for(const auto& next:after.fields){const auto* prior=Member(before,next);if(prior&&(next.access&0x10u))changed.push_back(&next);}
  native_schema_policy::Plan policy;
  if(!native_schema_policy::Plan::Prepare(before,after,&policy,error))return false;
  for(const auto& requirement:policy.Requirements())if(requirement.kind==native_schema_policy::GuardKind::AccessPermissions)s.needs_access=true;
  if(changed.empty())continue;
  if(!s.calibration_complete||!s.permission_env||!final_write_guard::ListenerProven(s.guard)||!Handles(s.bindings))return Fail(error,"Final-field transition lacks actual bytecode/MethodHandle/VarHandle permission proof");
  size_t index=s.staged.size();
  for(size_t n=0;n<s.staged.size();++n){void* old=nullptr;if(!s.bindings.native.decode_global(s.bindings.native.owner,s.staged[n].root,&old))return false;if(old==plan.old_classes[i]){if(index!=s.staged.size())return Fail(error,"Duplicate staged final-field Class");index=n;}}
  if(index==s.staged.size()||!Schema(before,s.staged[index].schema))return Fail(error,"Final-field native owner differs from the complete staged public schema");
  const auto view=s.bindings.native.metadata->Class(plan.old_classes[i],false);
  if(!view.descriptor||view.descriptor!=before.descriptor)return Fail(error,"Final-field Class/loader provenance unavailable");
  void* anchor=Field(*s.bindings.native.metadata,plan.old_classes[i],*changed.front(),error);
  void* next_anchor=Field(*s.bindings.native.metadata,plan.new_classes[i],*changed.front(),error);
  if(!anchor||!next_anchor||s.bindings.native.metadata->FieldDeclaringClass(next_anchor)!=plan.new_classes[i]||s.bindings.native.metadata->FieldDeclaringClass(anchor)!=plan.old_classes[i])return Fail(error,"Final-field obsolete owner anchor is not an exact original native Field");
  pending->aliases.push_back({s.staged[index].weak,anchor});
  pairs.push_back({i,index,anchor,next_anchor,std::move(before),std::move(after)});
 }
 if(s.needs_access&&(!s.calibration_complete||!s.permission_env||!final_write_guard::ListenerProven(s.guard)))return Fail(error,"Access evolution lacks actual process interpreter owner proof");
 s.rooted_pairs=std::move(rooted);
 s.expected=plan; // all full-Plan vector storage allocated in refundable Collected window
 s.pairs=std::move(pairs);s.pending=std::move(pending);s.prepared=true;return true;
}
bool Service::Seal(const source_commit::Plan& plan,std::string* error){auto& s=*state_;
 if(!s.prepared||s.committed||!redefinition_gate::Owned(s.bindings.native.vm)||plan.old_classes.size()!=plan.new_classes.size()||
    plan.old_classes.size()!=s.expected.old_classes.size()||plan.old_instances.size()!=s.expected.old_instances.size()||
    plan.new_instances.size()!=s.expected.new_instances.size()||plan.class_root_origins.size()!=s.expected.class_root_origins.size())return Fail(error,"Final-field full seal lacks its prepared source operation");
 if(plan.abstract_instances!=s.expected.abstract_instances)return Fail(error,"Prepared permission abstract mode changed");
 for(size_t i=0;i<plan.class_root_origins.size();++i)if(plan.class_root_origins[i].index!=s.expected.class_root_origins[i].index)return Fail(error,"Prepared permission root partition index changed");
 for(size_t i=0;i<s.rooted_pairs.size();++i){const auto& root=s.staged[s.rooted_pairs[i]];void *old=nullptr,*next=nullptr;
  if(!s.bindings.native.decode_global(s.bindings.native.owner,root.root,&old)||!s.bindings.native.decode_global(s.bindings.native.owner,root.replacement,&next)||old!=plan.old_classes[i]||next!=plan.new_classes[i])return Fail(error,"Full permission Plan differs from independently owned paired Class roots");
 }
 for(const auto& pair:s.pairs){void* old=nullptr;
  if(pair.index>=plan.old_classes.size()||!s.bindings.native.decode_global(s.bindings.native.owner,s.staged[pair.public_index].root,&old)||
     old!=plan.old_classes[pair.index]||s.bindings.native.metadata->FieldDeclaringClass(pair.old_anchor)!=old||
     s.bindings.native.metadata->FieldDeclaringClass(pair.new_anchor)!=plan.new_classes[pair.index])return Fail(error,"Prepared final-field arenas no longer belong to the exact live public/source Class pair");

 }
 // Rooted arrays are freshly decoded by the sole verifier. Patch only existing
 // storage; no native declaration/schema allocations or capability acquisition
 // occur after the irreversible outer Run boundary.
 s.expected.mirror=plan.mirror;s.expected.new_classes_root=plan.new_classes_root;
 s.expected.abstract_instances=plan.abstract_instances;
 std::copy(plan.old_classes.begin(),plan.old_classes.end(),s.expected.old_classes.begin());
 std::copy(plan.new_classes.begin(),plan.new_classes.end(),s.expected.new_classes.begin());
 std::copy(plan.old_instances.begin(),plan.old_instances.end(),s.expected.old_instances.begin());
 std::copy(plan.new_instances.begin(),plan.new_instances.end(),s.expected.new_instances.begin());
 std::copy(plan.class_root_origins.begin(),plan.class_root_origins.end(),s.expected.class_root_origins.begin());
 s.sealed=true;return true;
}
bool Service::VerifyAliases(const source_commit::Plan& plan,std::string* error)const{const auto& s=*state_;
 if(!s.prepared||!s.sealed||!redefinition_gate::Owned(s.bindings.native.vm)||!source_commit::SameIdentity(plan,s.expected))
  return Fail(error,"Alias proof lacks exact sealed source plan and operation owner");
 if(s.pairs.empty()&&!s.needs_access)return true;
 if(!s.calibration_complete||!s.permission_env||!final_write_guard::ListenerProven(s.guard)||!Handles(s.bindings))
  return Fail(error,"Alias/access proof lost behavioral process permission owner");
 if(plan.abstract_instances!=s.expected.abstract_instances)return Fail(error,"Prepared permission abstract mode changed");
 for(size_t i=0;i<plan.class_root_origins.size();++i)if(plan.class_root_origins[i].index!=s.expected.class_root_origins[i].index)return Fail(error,"Prepared permission root partition index changed");
 for(size_t i=0;i<s.rooted_pairs.size();++i){const auto& root=s.staged[s.rooted_pairs[i]];void *old=nullptr,*next=nullptr;
  if(!s.bindings.native.decode_global(s.bindings.native.owner,root.root,&old)||!s.bindings.native.decode_global(s.bindings.native.owner,root.replacement,&next)||old!=plan.old_classes[i]||next!=plan.new_classes[i])return Fail(error,"Full permission Plan differs from independently owned paired Class roots");
 }
 for(const auto& pair:s.pairs){void* old=nullptr;
  if(!s.bindings.native.decode_global(s.bindings.native.owner,s.staged[pair.public_index].root,&old)||old!=plan.old_classes[pair.index]||
     s.bindings.native.metadata->FieldDeclaringClass(pair.old_anchor)!=old||
     s.bindings.native.metadata->FieldDeclaringClass(pair.new_anchor)!=plan.new_classes[pair.index])return Fail(error,"Alias native field arena no longer belongs to same rooted Class pair");
 }
 return true;
}
bool Service::Verify(const source_commit::Plan& plan,std::string* error)const{
 return state_->needs_access?VerifyAliases(plan,error):Fail(error,"Access requirement was not prepared for this exact source operation");
}
bool Service::Verify(const source_commit::Plan& plan,size_t index,void* loader,const evolution::ClassSchema& before,const evolution::ClassSchema& after,const native_schema_policy::Requirement& requirement,std::string* error)const{const auto& s=*state_;
 if(requirement.kind!=native_schema_policy::GuardKind::FinalFields||!s.sealed||!redefinition_gate::Owned(s.bindings.native.vm)||
    !source_commit::SameIdentity(plan,s.expected)||!s.calibration_complete||!s.permission_env||!final_write_guard::ListenerProven(s.guard)||!Handles(s.bindings))return Fail(error,"Final-field requirement lacks its exact source plan and behavioral owner");
 auto pair=std::find_if(s.pairs.begin(),s.pairs.end(),[&](const auto& p){return p.index==index;});
 if(pair==s.pairs.end()||s.bindings.native.metadata->Class(plan.old_classes[index],false).loader!=loader||
    s.bindings.native.metadata->Class(plan.new_classes[index],false).loader!=loader||!Schema(pair->before,before)||!Schema(pair->after,after))return Fail(error,"Final-field pair/ClassLoader/schema differs from sealed permission proof");
 const auto next=std::find_if(after.fields.begin(),after.fields.end(),[&](const auto& f){return f.name==requirement.member_name&&f.descriptor==requirement.descriptor;});
 if(next==after.fields.end())return false;
 const auto* prior=Member(before,*next);
 if(!prior||!NeedsGuard(*prior,*next))return Fail(error,"Final-field requirement does not name a guarded surviving field identity");
 for(const auto& entry:std::array<std::pair<void*,const evolution::Member*>,2>{{{plan.old_classes[index],prior},{plan.new_classes[index],&*next}}}){
  void* field=Field(*s.bindings.native.metadata,entry.first,*entry.second,error);
  if(!field||s.bindings.native.metadata->FieldDeclaringClass(field)!=entry.first||
     native_schema_policy::JavaFlags(field_array::Flags(field))!=native_schema_policy::JavaFlags(entry.second->access))return Fail(error,"Final-field current native owner/flags changed after sealing");
 }
 return true;
}
void Service::BeginCommit(const source_commit::Plan& plan)const{auto& s=*state_;
 if(!s.sealed||!source_commit::SameIdentity(plan,s.expected)||!redefinition_gate::Owned(s.bindings.native.vm))std::abort();
 if(s.committed)return;
 if(!s.pairs.empty()||s.needs_access)final_write_guard::RetainInterpretationAtCommit();
 if(!s.pairs.empty()){
  s.aliases.Publish(std::move(s.pending));
  for(const auto& pair:s.pairs)s.staged[pair.public_index].weak=nullptr;
 }
 s.committed=true;
}
void Service::Clear(JNIEnv* env){if(!env)return;auto& s=*state_;
 for(auto& entry:s.staged){if(entry.root)env->DeleteGlobalRef(entry.root);if(entry.replacement)env->DeleteGlobalRef(entry.replacement);if(entry.weak)env->DeleteWeakGlobalRef(entry.weak);}
 s.staged.clear();s.expected={};s.rooted_pairs.clear();s.pairs.clear();s.pending.reset();s.prepared=false;s.sealed=false;s.committed=false;s.needs_access=false;
 // Test weak liveness with ordinary JNI outside the native alias mutex. After
 // deactivation, no future callback may decode the deleted handle; readers
 // already in the mutex have completed before it is acquired here.
 for(void* handle:s.aliases.WeakHandles()){
  jweak weak=static_cast<jweak>(handle);jobject local=env->NewLocalRef(weak);
  if(local){env->DeleteLocalRef(local);continue;}
  if(env->ExceptionCheck())continue;
  if(s.aliases.Deactivate(handle))env->DeleteWeakGlobalRef(weak);
 }
}
} // namespace artdcevm::final_field_service
