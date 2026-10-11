#include "platform/common/native_final_write_alias_batch.h"
#include "platform/common/redefinition_gate.h"
#include <cstdlib>
namespace artdcevm::final_write_guard {
namespace {bool Fail(std::string*error,const char*why){if(error)*error=why;return false;}}
bool AliasBatch::PublicWindow()const noexcept{
 return context_&&env_&&vm_&&self_&&context_->env==env_&&context_->vm==vm_&&context_->self==self_&&
  access_.current_()==self_&&roots_.ReadyForSourceAccess()&&redefinition_gate::Owned(vm_)&&
  native_source_stop::Session::InspectionAvailable()&&runnable_jni::Bridge::BorrowOriginalScope(*context_);
}
std::unique_ptr<AliasBatch>AliasBatch::Prepare(NativeAccess&access,const runnable_jni::Context&context,
 const source_handle_roots::Service&roots,std::string*error){
 if(!context.env||!context.vm||!context.self||context.env->ExceptionCheck()||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||!native_source_stop::Session::InspectionAvailable()||
    access.current_()!=context.self||!roots.ReadyForSourceAccess()||
    !redefinition_gate::Owned(static_cast<JavaVM*>(context.vm))||!access.references_.Ready()){
  Fail(error,"final-alias-preparation-requires-actual-original-Runnable-owner");return nullptr;
 }
 auto result=std::unique_ptr<AliasBatch>(new AliasBatch(access,context,roots));
 result->proof_=access.writer_.FreezeProof();
 if(!access.writer_.ValidateProof(result->proof_)){
  Fail(error,"final-alias-writer-four-path-proof-unavailable");return nullptr;
 }
 return result;
}
bool AliasBatch::StageClasses(const std::vector<jclass>&classes,std::string*error){
 if(phase_!=Phase::Created||classes.empty()||!PublicWindow()||env_->ExceptionCheck()||
    !access_.writer_.ValidateProof(proof_))return Fail(error,"final-alias-affected-cohort-staging-refused");
 phase_=Phase::Staging; // Failed/partial JNI registration cannot be retried.
 handles_.resize(classes.size());records_.reserve(classes.size());
 node_=std::make_unique<final_field_aliases::Node>();node_->aliases.reserve(classes.size());
 for(size_t i=0;i<classes.size();++i){
  if(!classes[i]||!PublicWindow())return Fail(error,"final-alias-public-Class-owner-unavailable");
  for(size_t j=0;j<i;++j)if(env_->IsSameObject(classes[i],handles_[j].strong))
   return Fail(error,"final-alias-public-Class-duplicate");
  if(env_->ExceptionCheck())return false;
  auto&r=handles_[i];r.strong=env_->NewGlobalRef(classes[i]);
  if(!r.strong||env_->ExceptionCheck())return false;
  if(!access_.references_.RegisterStrong(env_,r.strong,error))return false;r.strong_registered=true;
  r.weak=env_->NewWeakGlobalRef(r.strong);if(!r.weak||env_->ExceptionCheck())return false;
  if(!access_.references_.RegisterWeak(env_,r.weak,error))return false;r.weak_registered=true;
 }
 if(!PublicWindow()||env_->ExceptionCheck())return Fail(error,"final-alias-public-staging-scope-changed");
 phase_=Phase::Prepared;return true;
}
bool AliasBatch::Bind(const runnable_jni::Context&context,const native_source_stop::Session&stop,const source_commit::Plan&plan,
 const std::vector<modern_methods::Service*>&methods,
 const modern_field_metadata::ReadContext&read,std::string*error){
 if(phase_!=Phase::Prepared||context_!=&context||!PublicWindow()||stop.Reserved()||
    !read.owner||!read.extent||!node_||plan.old_classes.size()!=handles_.size()||
    plan.new_classes.size()!=handles_.size()||methods.size()!=handles_.size()||
    !access_.writer_.ValidateProof(proof_))return Fail(error,"final-alias-Collected-binding-owner-refused");
 phase_=Phase::Binding; // A partial native bind is terminal until Close.
 // Holder order is authoritative. Match the complete registered public
 // cohort by current strong-root identity, without JNI or raw roots across GC.
 for(size_t i=0;i<handles_.size();++i){size_t match=SIZE_MAX;
  for(size_t j=i;j<handles_.size();++j){void*klass=nullptr;
   if(!handles_[j].strong_registered||!handles_[j].weak_registered||
      !access_.references_.StrongRunnable(self_,handles_[j].strong,&klass)||!klass)
    return Fail(error,"final-alias-registered-cohort-root-refused");
   if(klass==plan.old_classes[i]){if(match!=SIZE_MAX)return Fail(error,"final-alias-Holder-Class-ambiguous");match=j;}
  }
  if(match==SIZE_MAX)return Fail(error,"final-alias-Holder-Class-outside-affected-cohort");
  if(match!=i)std::swap(handles_[i],handles_[match]);
 }
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
 stop_=&stop;plan_=&plan;read_=&read;
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
 return Permissions(plan,read);
}
bool AliasBatch::Verify(const source_commit::Plan&plan,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read)const noexcept{
 return &stop==stop_&&stop.IsStopped(self)&&Match(plan,self,read)&&stop.IsStopped(self);
}
void AliasBatch::Publish(const source_commit::Plan&plan,const native_source_stop::Session&stop,void*self,
 const modern_field_metadata::ReadContext&read)const noexcept{
 if(!stop.IsFinalWindow(self)||!Verify(plan,stop,self,read))std::abort();
 access_.writer_.CommitProof(proof_);
 if(!node_->aliases.empty())access_.aliases_.Publish(std::move(node_));
 // Empty native storage is disposed by Close after original Run, not here.
 for(const auto&r:records_)handles_[r.root].transferred=true;
 published_=true;phase_=Phase::Published;
}
bool AliasBatch::Ordinary()const noexcept{
 return stop_&&!stop_->Reserved()&&PublicWindow();
}
bool AliasBatch::Permissions(const source_commit::Plan&plan,
 const modern_field_metadata::ReadContext&read)const noexcept{
 for(const auto&p:permissions_){
  if(p.pair>=plan.old_classes.size()||p.pair>=plan.new_classes.size())return false;
  const auto member=[&](void*klass,void*field,uint32_t flags,uint32_t dex_index){
   field_array::View fields;uint32_t index=0;
   if(!access_.metadata_.Fields(klass,p.statics,&fields,nullptr,&read)||!fields.IndexOf(field,&index)||
      access_.metadata_.FieldDeclaringClass(field)!=klass)return false;
   const auto*native=static_cast<const field_array::Field*>(field);
   return (native->access_flags&0xffffu)==flags&&native->dex_field_index==dex_index;
  };
  if(!member(plan.old_classes[p.pair],p.before,p.before_flags,p.before_index)||
     !member(plan.new_classes[p.pair],p.after,p.after_flags,p.after_index)||
     access_.metadata_.Reference(plan.old_classes[p.pair],offsetof(class_metadata::Class11,class_loader))!=
     access_.metadata_.Reference(plan.new_classes[p.pair],offsetof(class_metadata::Class11,class_loader)))return false;
 }
 return true;
}
bool AliasBatch::VerifyAliases(const source_commit::Plan&plan,std::string*error)const{
 if(!stop_||!read_||!redefinition_gate::Owned(vm_))return Fail(error,"final-alias-transaction-owner-unavailable");
 if(stop_->IsStopped(self_)){
  if(Verify(plan,*stop_,self_,*read_))return true;
 }else if(Ordinary()&&Match(plan,self_,*read_)&&Ordinary())return true;
 return Fail(error,"final-alias-exact-Plan-or-live-window-refused");
}
bool AliasBatch::RefreshPlan(const source_commit::Plan&plan,std::string*error)const{
 if(!stop_||!read_||!stop_->IsStopped(self_)||!redefinition_gate::Owned(vm_)||
    !Verify(plan,*stop_,self_,*read_))return Fail(error,"final-fields-stopped-root-refresh-refused");
 return true;
}
bool AliasBatch::Verify(const source_commit::Plan&plan,size_t pair,void*loader,
 const evolution::ClassSchema&before,const evolution::ClassSchema&after,
 const native_schema_policy::Requirement&requirement,std::string*error)const{
 if(!Ordinary()||!VerifyAliases(plan,error)||pair>=plan.old_classes.size()||
    requirement.kind!=native_schema_policy::GuardKind::FinalFields||before.descriptor!=after.descriptor)
  return Fail(error,"final-fields-ordinary-permission-owner-refused");
 const auto declaration=[&](const evolution::ClassSchema&schema)->const evolution::Member*{
  const evolution::Member*found=nullptr;
  for(const auto&field:schema.fields)if(field.name==requirement.member_name&&field.descriptor==requirement.descriptor){
   if(found)return nullptr;found=&field;
  }
  return found;
 };
 const auto*old=declaration(before);const auto*next=declaration(after);
 if(!old||!next||old->IsStatic()!=next->IsStatic()||!(next->access&0x10u)||
    !((old->access^next->access)&0x17u))return Fail(error,"final-fields-exact-declaration-change-refused");
 const auto native=[&](void*klass,const evolution::Member&member)->void*{
  field_copy::CopyClassView identity{};field_array::View fields;
  if(!access_.metadata_.ClassIdentity(klass,&identity,error,read_)||identity.loader!=loader||
     !identity.descriptor||before.descriptor!=identity.descriptor||
     !access_.metadata_.Fields(klass,member.IsStatic(),&fields,error,read_))return nullptr;
  void*found=nullptr;
  for(uint32_t n=0;n<fields.selected_count;++n){auto*field=fields.AtSelected(n);field_copy::CopyFieldView text{};
   if(!access_.metadata_.FieldIdentity(field,&text,error,read_))return nullptr;
   if(!text.name||!text.descriptor||member.name!=text.name||member.descriptor!=text.descriptor)continue;
   const auto*f=static_cast<const field_array::Field*>(field);
   if(found||text.field!=field||access_.metadata_.FieldDeclaringClass(field)!=klass||
      (f->access_flags&0xffffu)!=(member.access&0xffffu)||f->dex_field_index!=member.dex_index)return nullptr;
   found=field;
  }
  return found;
 };
 void*old_field=native(plan.old_classes[pair],*old);if(!old_field)return Fail(error,"final-fields-native-original-declaration-refused");
 void*new_field=native(plan.new_classes[pair],*next);if(!new_field)return Fail(error,"final-fields-native-replacement-declaration-refused");
 Permission permission{pair,old_field,new_field,old->IsStatic(),old->access&0xffffu,next->access&0xffffu,old->dex_index,next->dex_index};
 for(const auto&p:permissions_)if(p.pair==pair&&p.before==old_field&&p.after==new_field)
  return p.statics==permission.statics&&p.before_flags==permission.before_flags&&p.after_flags==permission.after_flags&&
   p.before_index==permission.before_index&&p.after_index==permission.after_index&&Ordinary();
 if(!Ordinary())return Fail(error,"final-fields-original-scope-changed");
 permissions_.push_back(permission);return true;
}
void AliasBatch::BeginCommit(const source_commit::Plan&plan)const{
 if(!stop_||!read_||!redefinition_gate::Owned(vm_))std::abort();
 Publish(plan,*stop_,self_,*read_);
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
 if(!PublicWindow())return false;
 // Native metadata is never touched after original Run destroys private data.
 node_.reset();records_.clear();permissions_.clear();plan_=nullptr;read_=nullptr;context_=nullptr;stop_=nullptr;
 for(auto&r:handles_){
  if(r.weak&&!r.transferred){if(r.weak_registered)access_.references_.Forget(r.weak);env_->DeleteWeakGlobalRef(r.weak);}
  if(r.strong){if(r.strong_registered)access_.references_.Forget(r.strong);env_->DeleteGlobalRef(r.strong);}
  r={};
 }
 phase_=Phase::Closed;return true;
}
AliasBatch::~AliasBatch(){if(!Close())std::abort();}
}
