#include "platform/common/native_final_write_alias_batch.h"
#include "platform/common/redefinition_gate.h"
#include <cstdlib>
namespace artdcevm::final_write_guard {
namespace {bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}}
bool AliasBatch::Native()const noexcept{
 return env_&&vm_&&self_&&access_.current_()==self_&&
  roots_.OrdinaryNative({self_,env_,vm_});
}
std::unique_ptr<AliasBatch>AliasBatch::Prepare(NativeAccess&access,JNIEnv*env,
 const source_handle_roots::Service&roots,const std::vector<jclass>&classes,std::string*error){
 JavaVM*vm=nullptr;void*self=access.current_();
 if(!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||!vm||!self||classes.empty()||
    !roots.Calibrated()||!roots.OrdinaryNative({self,env,vm})||!redefinition_gate::Owned(vm)||
    !access.references_.Ready()){
  Fail(error,"final-alias-preparation-requires-actual-ordinary-Native-owner");return nullptr;
 }
 auto result=std::unique_ptr<AliasBatch>(new AliasBatch(access,env,roots,vm,self));
 result->proof_=access.writer_.FreezeProof();
 if(!access.writer_.ValidateProof(result->proof_)){
  Fail(error,"final-alias-writer-four-path-proof-unavailable");return nullptr;
 }
 result->handles_.resize(classes.size());result->records_.reserve(classes.size());
 result->node_=std::make_unique<final_field_aliases::Node>();result->node_->aliases.reserve(classes.size());
 for(size_t i=0;i<classes.size();++i){
  if(!classes[i]||!result->Native()){Fail(error,"final-alias-public-Class-owner-unavailable");return nullptr;}
  for(size_t j=0;j<i;++j)if(env->IsSameObject(classes[i],result->handles_[j].strong)){
   Fail(error,"final-alias-public-Class-duplicate");return nullptr;
  }
  if(env->ExceptionCheck())return nullptr;
  auto&r=result->handles_[i];r.strong=env->NewGlobalRef(classes[i]);
  if(!r.strong||env->ExceptionCheck())return nullptr;
  if(!access.references_.RegisterStrong(env,r.strong,error))return nullptr;r.strong_registered=true;
  r.weak=env->NewWeakGlobalRef(r.strong);if(!r.weak||env->ExceptionCheck())return nullptr;
  if(!access.references_.RegisterWeak(env,r.weak,error))return nullptr;r.weak_registered=true;
 }
 return result;
}
bool AliasBatch::Bind(const runnable_jni::Context&context,const source_commit::Plan&plan,
 const std::vector<modern_methods::Service*>&methods,
 const modern_field_metadata::ReadContext&read,std::string*error){
 if(phase_!=Phase::Prepared||context.env!=env_||context.vm!=vm_||context.self!=self_||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||!redefinition_gate::Owned(vm_)||
    !read.owner||!read.extent||!node_||plan.old_classes.size()!=handles_.size()||
    plan.new_classes.size()!=handles_.size()||methods.size()!=handles_.size()||
    !access_.writer_.ValidateProof(proof_))return Fail(error,"final-alias-Collected-binding-owner-refused");
 phase_=Phase::Binding; // A partial native bind is terminal until Close.
 for(size_t i=0;i<handles_.size();++i){void*original=nullptr;auto*m=methods[i];
  if(!access_.references_.StrongRunnable(self_,handles_[i].strong,&original)||original!=plan.old_classes[i]||
     !original||!plan.new_classes[i]||original==plan.new_classes[i]||!m||!m->UsesReadContext(read)||
     !m->MatchesClassTable(original)||!m->MatchesClassTable(plan.new_classes[i]))
   return Fail(error,"final-alias-rooted-Plan-or-method-table-refused");
  handles_[i].methods=m;
  final_field_aliases::Alias alias;alias.weak=handles_[i].weak;
  // Prefer a real declared method. Classes with fields only use the same
  // typed anchor path; copied/interface methods cannot anchor this Class.
  for(size_t n=0;n<m->Count(original);++n){auto*member=m->At(original,n);
   if(member&&m->DeclaringClass(member)==original){alias.member=member;alias.kind=final_field_aliases::MemberKind::Method;break;}
  }
  if(!alias.member)for(bool statics:{false,true}){field_array::View fields;
   if(!access_.metadata_.Fields(original,statics,&fields,error,&read))return false;
   if(fields.selected_count){alias.member=fields.AtSelected(0);alias.kind=final_field_aliases::MemberKind::Field;break;}
  }
  // No retained member means no old bytecode writer from this Class. Do not
  // publish an empty anchor or transfer a weak handle with no alias consumer.
  if(!alias.member)continue;
  Record record{i,m};if(!Member(alias,record,original,read))return Fail(error,"final-alias-native-member-anchor-refused");
  node_->aliases.push_back(alias);records_.push_back(record);
 }
 plan_=&plan;read_=&read;
 storage_={plan.old_classes.data(),plan.new_classes.data(),plan.old_instances.data(),plan.new_instances.data(),plan.class_root_origins.data()};
 counts_={plan.old_classes.size(),plan.new_classes.size(),plan.old_instances.size(),plan.new_instances.size(),plan.class_root_origins.size()};
 phase_=Phase::Bound;return true;
}
bool AliasBatch::Member(const final_field_aliases::Alias&alias,const Record&record,void*klass,
 const modern_field_metadata::ReadContext&read)const noexcept{
 size_t extent=0;if(!alias.member||!record.methods||!read.extent(read.owner,alias.member,&extent))return false;
 if(alias.kind==final_field_aliases::MemberKind::Method){
  if(extent<8||!record.methods->UsesReadContext(read))return false;
  for(size_t n=0;n<record.methods->Count(klass);++n)if(record.methods->At(klass,n)==alias.member)
   return access_.metadata_.MethodDeclaringClass(alias.member)==klass;
  return false;
 }
 if(alias.kind!=final_field_aliases::MemberKind::Field||extent<sizeof(field_array::Field))return false;
 for(bool statics:{false,true}){field_array::View fields;
  if(!access_.metadata_.Fields(klass,statics,&fields,nullptr,&read))return false;
  uint32_t index=0;if(fields.IndexOf(alias.member,&index))return access_.metadata_.FieldDeclaringClass(alias.member)==klass;
 }
 return false;
}
bool AliasBatch::Match(const source_commit::Plan&plan,void*self,
 const modern_field_metadata::ReadContext&read)const noexcept{
 if(phase_!=Phase::Bound||self!=self_||access_.current_()!=self_||&plan!=plan_||&read!=read_||
    !read.owner||!read.extent||!node_||!access_.writer_.ValidateProof(proof_)||
    storage_!=std::array<const void*,5>{plan.old_classes.data(),plan.new_classes.data(),plan.old_instances.data(),plan.new_instances.data(),plan.class_root_origins.data()}||
    counts_!=std::array<size_t,5>{plan.old_classes.size(),plan.new_classes.size(),plan.old_instances.size(),plan.new_instances.size(),plan.class_root_origins.size()}||
    node_->aliases.size()!=records_.size()||plan.old_classes.size()!=handles_.size())return false;
 for(size_t i=0;i<handles_.size();++i){void*original=nullptr;
  if(!handles_[i].strong_registered||!handles_[i].weak_registered||handles_[i].transferred||
     !access_.references_.Strong({self,env_,vm_},native_reference_decoder::Scope::NoSuspend,handles_[i].strong,&original)||original!=plan.old_classes[i]||
     !original||!plan.new_classes[i]||original==plan.new_classes[i]||!handles_[i].methods||
     !handles_[i].methods->UsesReadContext(read)||!handles_[i].methods->MatchesClassTable(original)||
     !handles_[i].methods->MatchesClassTable(plan.new_classes[i]))return false;
 }
 for(size_t i=0;i<records_.size();++i){const auto&r=records_[i];const auto&a=node_->aliases[i];
  if(r.root>=handles_.size()||a.weak!=handles_[r.root].weak||a.retiring||a.readers||
     !Member(a,r,plan.old_classes[r.root],read))return false;
 }
 return true;
}
bool AliasBatch::Verify(const source_commit::Plan&plan,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read)const noexcept{
 return stop.IsStopped(self)&&Match(plan,self,read)&&stop.IsStopped(self);
}
void AliasBatch::Publish(const source_commit::Plan&plan,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read)noexcept{
 if(!stop.IsFinalWindow(self)||!Verify(plan,stop,self,read))std::abort();
 access_.writer_.CommitProof(proof_);
 if(!node_->aliases.empty())access_.aliases_.Publish(std::move(node_));
 // Empty native storage is disposed by Close in ordinary Native, not here.
 for(const auto&r:records_)handles_[r.root].transferred=true;
 published_=true;phase_=Phase::Published;
}
bool AliasBatch::Cleanup(NativeAccess&access,JNIEnv*env,
 const source_handle_roots::Service&roots,std::string*error){
 JavaVM*vm=nullptr;void*self=access.current_();
 if(!env||env->ExceptionCheck()||env->GetJavaVM(&vm)!=JNI_OK||!vm||!self||
    !roots.Calibrated()||!roots.OrdinaryNative({self,env,vm})||!redefinition_gate::Owned(vm))
  return Fail(error,"final-alias-cleanup-requires-actual-ordinary-Native-owner");
 std::lock_guard<std::mutex>lock(access.alias_cleanup_);
 const auto handles=access.aliases_.WeakHandles();
 for(auto*handle:handles){
  if(!roots.OrdinaryNative({self,env,vm}))return Fail(error,"final-alias-cleanup-Native-owner-changed");
  const bool cleared=env->IsSameObject(reinterpret_cast<jweak>(handle),nullptr);
  if(env->ExceptionCheck())return false;
  if(!cleared)continue;
  // JNI has returned to Native. Waiting here cannot retain a mutator borrow
  // needed by the in-flight weak reader whose lease Deactivate must drain.
  if(!roots.OrdinaryNative({self,env,vm})||!access.aliases_.Deactivate(handle))
   return Fail(error,"final-alias-cleanup-lease-retirement-refused");
  access.references_.Forget(reinterpret_cast<jweak>(handle));
  env->DeleteWeakGlobalRef(reinterpret_cast<jweak>(handle));
 }
 return true;
}
bool AliasBatch::Close()noexcept{
 if(phase_==Phase::Closed)return true;
 if(!Native())return false;
 // Native metadata is never touched after original Run destroys private data.
 node_.reset();records_.clear();plan_=nullptr;read_=nullptr;
 for(auto&r:handles_){
  if(r.weak&&!r.transferred){if(r.weak_registered)access_.references_.Forget(r.weak);env_->DeleteWeakGlobalRef(r.weak);}
  if(r.strong){if(r.strong_registered)access_.references_.Forget(r.strong);env_->DeleteGlobalRef(r.strong);}
  r={};
 }
 phase_=Phase::Closed;return true;
}
AliasBatch::~AliasBatch(){if(!Close())std::abort();}
}
