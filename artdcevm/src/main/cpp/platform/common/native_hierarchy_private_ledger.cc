#include "platform/common/native_hierarchy_private_ledger.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/native_source_gc_publication.h"
#include "platform/abi/arm64/common/redefiner_prefix.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <new>
namespace artdcevm::native_hierarchy_private_ledger {
namespace {
thread_local Ledger*current=nullptr;
using Object=abi::arm64::android11::ReleaseObjectPointer;
using Register=void(*)(void*,Object,Object);
Register original=nullptr;void*installed_linker=nullptr;std::mutex installation;
bool Error(std::string*e,const char*s){if(e)*e=s;return false;}
bool Mapped(const void*p,size_t n){size_t e=0;return p&&method_identity::ReadableMappingExtent(p,&e)&&e>=n;}
bool Same(const native_hierarchy_source_cache::RecordView&a,const native_hierarchy_source_cache::RecordView&b){return a.source_owner==b.source_owner&&a.class_index==b.class_index&&a.old_root==b.old_root&&a.new_root==b.new_root&&a.cache_root==b.cache_root&&a.parent_root==b.parent_root&&a.declarations==b.declarations&&a.dex.begin==b.dex.begin&&a.dex.size==b.dex.size&&a.dex.data_begin==b.dex.data_begin&&a.dex.data_size==b.dex.data_size&&a.dex.class_def_index==b.dex.class_def_index;}
}
namespace detail {struct Hooks {
 static void Proxy(void*l,Object c,Object loader){
  auto*owner=current;auto*entry=owner?owner->Publication(l,reinterpret_cast<void*>(c.reference),reinterpret_cast<void*>(loader.reference)):nullptr;
  {
   // The sole named registration wrapper owns the entire original callee.
   // A calibrated nested GC lease cannot escape or be forged by another caller.
   native_source_gc_publication::Scope scope(l,reinterpret_cast<void*>(c.reference),reinterpret_cast<void*>(loader.reference));
   original(l,c,loader); // Preserve ordinary registration when no Run is active.
   scope.Returned();
  }
  if(entry)owner->Published(entry);
 }
};}
Ledger::Ledger(void*d,const runnable_jni::Context&c,source_handle_roots::Pool&p,const native_hierarchy_source_cache::BatchAccess&b,const modern_field_metadata::FieldMetadata&f,const native_source_dex_registry::Service&r,const CleanupOwner*cleanup)
 :driver_(d),context_(c),pool_(p),batch_(b),fields_(f),registry_(r),cleanup_(cleanup),previous_(current){
 if(current||!installed_linker||!registry_.Calibrated()||!fields_.Calibrated()||!pool_.ReadyForSourceAccess()||!Mapped(d,sizeof(abi::arm64::android_common::RedefinerPrefix)))return;
 abi::arm64::android_common::RedefinerPrefix prefix;std::memcpy(&prefix,d,sizeof(prefix));
 if(!c.env||!c.self||prefix.self!=c.self||!prefix.environment||prefix.type||prefix.result!=113)return;
 current=this;active_=true;
}
Ledger::~Ledger(){if(active_&&!released_)std::abort();}
bool Ledger::Live()const noexcept{return active_&&!released_&&!failed_&&!refunded_&&current==this&&((inspection_reader_&&inspection_reader_->FinalRootWindow())||FinalRootWindow()||PublicationRootWindow()||pool_.ReadyForSourceAccess());}
bool Ledger::EnterSemanticInspection(const source_handle_roots::StoppedReader& reader)const noexcept{
 if(inspection_reader_||!active_||released_||failed_||refunded_||current!=this||
    FinalRootWindow()||PublicationRootWindow()||!reader.FinalRootWindow())return false;
 inspection_reader_=&reader;return true;
}
void Ledger::LeaveSemanticInspection()const noexcept{
 if(!inspection_reader_)std::abort();
 inspection_reader_=nullptr;
}
bool Ledger::FinalRootWindow()const noexcept{return active_&&!released_&&!failed_&&!refunded_&&current==this&&roots_refreshed_&&cleanup_&&cleanup_->IsStopped(driver_,holder_);}
bool Ledger::ReadStoppedRoot(const void*token,void**out)const noexcept{
 if(!out||!FinalRootWindow())return false;
 return ReadRegisteredRoot(token,out);
}
bool Ledger::ReadRegisteredRoot(const void*token,void**out)const noexcept{
 if(!out||!roots_registered_||!roots_refreshed_)return false;
 for(const auto&e:root_entries_)if(e.token==token){*out=e.value;return true;}return false;
}
bool Ledger::Read(const void*token,void**out)const noexcept{return inspection_reader_?pool_.ReadStopped(*inspection_reader_,token,out):FinalRootWindow()?ReadStoppedRoot(token,out):PublicationRootWindow()?ReadRegisteredRoot(token,out):pool_.Read(token,out);}
bool Ledger::CollectManagedRoots(const prepared_roots::Registrar&r){
 if(!Live()||!collected_||registration_started_||!r.add||FinalRootWindow()||!pool_.ReadyForSourceAccess())return false;
 registration_started_=true;
 try{
  // Finish all native vector allocations BEFORE registering pointer locations.
  if(!pool_.VisitUsedSlots([&](void*token){root_entries_.push_back({token,nullptr});return true;}))return false;
  for(auto&e:root_entries_){if(!pool_.Read(e.token,&e.value)||!managed_roots_.Add(&e.value,r))return false;}
  managed_roots_.Seal();roots_registered_=true;return true;
 }catch(const std::bad_alloc&){return false;}
}
bool Ledger::CanRefresh(const prepared_roots::Resolver&r)const noexcept{return roots_registered_&&managed_roots_.CanRefresh(r);}
void Ledger::Refresh(const prepared_roots::Resolver&r)noexcept{if(!CanRefresh(r)||!cleanup_||!cleanup_->IsStopped(driver_,holder_))std::abort();managed_roots_.Refresh(r);roots_refreshed_=true;}
bool Ledger::Paused()const noexcept{auto*r=native_source_class_loading::Run::Current();return Live()&&r&&r->OwnsDriver(driver_)&&r->Paused();}
bool Ledger::AttachHolder(const transaction_root::Holder*h,const transaction_root::Metadata&m)noexcept{
 if(!Paused()||holder_||!h||!m.count||!m.stride)return false;
 if(!Mapped(h,sizeof(*h)))return false;
 transaction_root::Holder header{};transaction_root::Metadata actual{};
 if(!transaction_root::ReadHolderHeader(h,sizeof(*h),&header)||!Mapped(header.redefinitions,sizeof(transaction_root::Owners))||!transaction_root::ReadMetadata(header,m.count,transaction_root::OwnerFamily::kAndroid15,&actual)||actual.stride!=m.stride||actual.owners.begin!=m.owners.begin||actual.owners.end!=m.owners.end||actual.owners.capacity!=m.owners.capacity||actual.initialized.begin!=m.initialized.begin||actual.actually_structural.begin!=m.actually_structural.begin||actual.initial_structural.begin!=m.initial_structural.begin)return false;

 for(size_t i=0;i<m.count;++i){const void*o=m.owners.begin+i*m.stride;if(!Mapped(o,sizeof(abi::arm64::android_common::ClassRedefinitionPrefix)))return false;
 abi::arm64::android_common::ClassRedefinitionPrefix p;std::memcpy(&p,o,sizeof(p));if(p.driver!=driver_)return false;}
 holder_=h;metadata_=m;return true;
}
bool Ledger::Record(const native_hierarchy_source_cache::RecordView&r)const noexcept{
 if(!holder_||!r.source_owner||!r.declarations)return false;
 bool owner=false;for(size_t i=0;i<metadata_.count;++i)if(r.source_owner==metadata_.owners.begin+i*metadata_.stride)owner=true;
 if(!owner)return false;
 for(size_t i=0;i<batch_.Count();++i){native_hierarchy_source_cache::RecordView actual;if(!batch_.At(i,&actual))return false;if(Same(actual,r))return true;}
 return false;
}
const Ledger::Entry*Ledger::Find(const native_hierarchy_cache::Source&s)const noexcept{
 for(const auto&e:entries_)if(e.source.private_token==s.private_token&&e.source.cache_root==s.cache_root&&e.source.dex_identity==s.dex_identity&&e.source.cache_extent==s.cache_extent&&e.source.counts.types==s.counts.types&&e.source.counts.fields==s.counts.fields&&e.source.counts.methods==s.counts.methods)return &e;
 return nullptr;
}
bool Ledger::VerifyStaged(const native_hierarchy_graph::Graph&g,const std::vector<native_hierarchy_graph::Input>&i)const noexcept{
 if(!Live()||!cleanup_||holder_||batch_.Count()||!entries_.empty()||i.empty()||g.inputs.size()!=i.size()||(graph_&&graph_!=&g))return false;
 graph_=&g;return true;
}
bool Ledger::PreparePrivate(const native_hierarchy_source_cache::RecordView&r,const native_hierarchy_cache::Source&s,std::string*error)const{
 if(!Paused()||collected_||registration_started_||!graph_||!Record(r)||!s.private_token||s.cache_root!=r.cache_root||Find(s))return Error(error,"Private cache registration requires exact source Holder and first allocation");
 void*replacement=nullptr,*old=nullptr;if(!Read(r.new_root,&replacement)||replacement||!Read(r.old_root,&old)||!old)return false;
 try{
  if(!registry_.Unregistered(context_,pool_,r.cache_root,s.dex_identity,error))return false;
  // All native query returns may have moved the Class, cache and loader.
  if(!Read(r.old_root,&old)||!old)return false;
  if(!fields_.Reference(old,offsetof(class_metadata::Class11,class_loader)))return Error(error,"Boot cache publication requires an independently owned AppendToBootClassPath ABI");
  auto*loader=pool_.Add(fields_.Reference(old,offsetof(class_metadata::Class11,class_loader)),error);if(!loader)return false;
  void*cache=nullptr;if(!Read(r.cache_root,&cache)||!cache)return false;
  for(const auto&e:entries_){void*existing=nullptr;if(!Read(e.record.cache_root,&existing))return false;if(existing==cache||e.source.dex_identity==s.dex_identity||e.source.private_token==s.private_token)return Error(error,"Private cache/DEX allocation or token was reused");}
  entries_.push_back({r,s,loader,false,false});return true;
 }catch(const std::bad_alloc&){return Error(error,"Private cache ledger exhausted native storage");}
}
bool Ledger::VerifyPrivate(const native_hierarchy_source_cache::RecordView&r,const native_hierarchy_cache::Source&s,const runnable_jni::Context&c,const source_handle_roots::Pool&p)const noexcept{
 const auto*e=Find(s);return Paused()&&!collected_&&!committed_&&c.self==context_.self&&c.env==context_.env&&&p==&pool_&&e&&!e->publishing&&!e->published&&Same(e->record,r)&&Record(r);
}
bool Ledger::ExactPlan(const source_commit::Plan&p)const noexcept{
 if(!Live()||!holder_||entries_.empty()||p.old_classes.size()!=p.new_classes.size()||p.old_classes.empty()||batch_.Count()!=entries_.size())return false;
 for(const auto&e:entries_){void*old=nullptr,*replacement=nullptr;if(!Record(e.record)||!Read(e.record.old_root,&old)||!Read(e.record.new_root,&replacement)||!old||!replacement||old==replacement)return false;
 size_t found=0;for(size_t i=0;i<p.old_classes.size();++i)if(p.old_classes[i]==old&&p.new_classes[i]==replacement)++found;if(found!=1)return false;}
 return p.old_classes.size()==entries_.size();
}
bool Ledger::Collected(const source_commit::Plan&p)noexcept{if(collected_)return ExactPlan(p);if(!Paused()||!ExactPlan(p))return false;collected_=true;return true;}
bool Ledger::PreparePublication(const native_source_stop::Session&stop,size_t*out,std::string*error){
 if(!out||publication_stop_||!Paused()||!collected_||registration_started_||!metadata_.count||entries_.size()!=metadata_.count)return Error(error,"DEX publication requires the complete ordinary source owner cohort");
 for(size_t i=0;i<metadata_.count;++i){
  const void*owner=metadata_.owners.begin+i*metadata_.stride;const Entry*found=nullptr;
  for(const auto&e:entries_)if(e.record.source_owner==owner){if(found)return Error(error,"DEX publication source owner is repeated");found=&e;}
  void*cache=nullptr,*loader=nullptr;
  if(!found||found->publishing||found->published||!Read(found->record.cache_root,&cache)||!Read(found->loader_root,&loader)||!cache||!loader)return Error(error,"DEX publication source owner/cache/loader is incomplete");
  for(const auto&e:entries_)if(&e!=found){void*other=nullptr;if(!Read(e.record.cache_root,&other)||other==cache)return Error(error,"DEX publication caches are ambiguous");}
 }
 publication_stop_=&stop;*out=metadata_.count;return true;
}
bool Ledger::PublicationRootWindow()const noexcept{
 return active_&&!released_&&!failed_&&!refunded_&&current==this&&collected_&&roots_registered_&&roots_refreshed_&&publication_stop_&&publication_stop_->CanPublishDex(driver_,holder_,installed_linker);
}
bool Ledger::MatchPublication(void*driver,const void*holder,void*linker,void*cache,void*loader,size_t*out)const noexcept{
 if(!out||!cache||!loader||driver!=driver_||holder!=holder_||linker!=installed_linker||!PublicationRootWindow())return false;
 const Entry*found=nullptr;
 for(const auto&e:entries_){void*expected_cache=nullptr,*expected_loader=nullptr;if(!ReadRegisteredRoot(e.record.cache_root,&expected_cache)||!ReadRegisteredRoot(e.loader_root,&expected_loader))return false;
  if(expected_cache==cache&&expected_loader==loader){if(found)return false;found=&e;}
 }
 if(!found)return false;
 for(size_t i=0;i<metadata_.count;++i)if(found->record.source_owner==metadata_.owners.begin+i*metadata_.stride){*out=i;return true;}
 return false;
}
Ledger::Entry*Ledger::Publication(void*l,void*cache,void*loader)noexcept{
 if(!Live()||l!=installed_linker)return nullptr;
 for(auto&e:entries_){void*c=nullptr;if(!Read(e.record.cache_root,&c)){failed_=true;return nullptr;}if(c!=cache)continue;
 void*expected=nullptr;if(!collected_||!Paused()||e.published||e.publishing||!Read(e.loader_root,&expected)||!loader||expected!=loader){failed_=true;return nullptr;}
 e.publishing=true;return &e;}
 return nullptr; // unrelated native registry operation preserves original.
}
void Ledger::Published(Entry*e)noexcept{if(!e||!e->publishing||!Live())std::abort();e->publishing=false;e->published=true;}
bool Ledger::VerifyCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{
 if(graph_!=&g||!collected_||committed_||!cleanup_||!ExactPlan(p))return false;
 for(const auto&e:entries_)if(e.publishing||(cleanup_->IsFinalWindow(driver_,holder_)&&!e.published))return false;
 return cleanup_->Verify(driver_,holder_,p);
}
bool Ledger::VerifyFrozen(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,const native_hierarchy_cache::Source&s)const noexcept{
 const auto*e=Find(s);if(!Live()||graph_!=&g||!e||e->publishing||p.source_owner!=e->record.source_owner||p.source_old_root!=e->record.old_root||p.class_index!=e->record.class_index||p.declarations!=e->record.declarations)return false;
 if(collected_){if(!cleanup_||(cleanup_->IsFinalWindow(driver_,holder_)&&!e->published))return false;}
 else if(!Paused()||e->published)return false;
 void*old=nullptr,*replacement=nullptr,*cache=nullptr,*parent=nullptr;
 return Read(e->record.old_root,&old)&&Read(e->record.new_root,&replacement)&&Read(e->record.cache_root,&cache)&&Read(e->record.parent_root,&parent)&&old==p.old_class&&replacement==p.new_class&&cache==p.cache&&parent==p.parent;
}
void Ledger::BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{if(!VerifyCommit(g,p)||!cleanup_->IsFinalWindow(driver_,holder_))std::abort();cleanup_->BeginCommit(p);committed_=true;}
bool Ledger::CanDiscard(const native_hierarchy_source_cache::RecordView&r,const char** refusal)const noexcept{
 const auto reject=[&](const char* detail){if(refusal)*refusal=detail;return false;};
 if(refusal)*refusal=nullptr;
 auto*loading=native_source_class_loading::Run::Current();
 if(!active_||released_||committed_||refunded_||current!=this)return reject("private-discard-ledger-owner-or-phase-refused");
 if(!loading||!loading->OwnsDriver(driver_))return reject("private-discard-source-loading-owner-refused");
 if(!loading->RefundWindow())return reject("private-discard-source-loading-RefundWindow-refused");
 if(!Record(r))return reject("private-discard-source-Holder-record-refused");
 if(!pool_.ReadyForSourceAccess())return reject("private-discard-ledger-roots-not-Runnable");
 const Entry*found=nullptr;
 for(const auto&e:entries_)if(Same(e.record,r)){if(found)return reject("private-discard-duplicate-ledger-record");found=&e;}
 if(!found)return reject("private-discard-private-cache-record-missing");
 if(found->publishing||found->published)return reject("private-discard-source-publication-already-begun");
 if(!Mapped(r.source_owner,sizeof(abi::arm64::android_common::ClassRedefinitionPrefix)))return reject("private-discard-source-definition-unmapped");
 abi::arm64::android_common::ClassRedefinitionPrefix owner;std::memcpy(&owner,r.source_owner,sizeof(owner));
 if(owner.driver!=driver_)return reject("private-discard-source-definition-driver-differs");
 if(owner.dex_file!=found->source.dex_identity)return reject("private-discard-source-definition-DexFile-differs");
 // Original nullable registry query, with fresh root rereads around the callee.
 return registry_.Unregistered(context_,pool_,r.cache_root,found->source.dex_identity,nullptr,refusal);
}
void Ledger::Refunded()noexcept{if(!active_||released_||committed_)std::abort();for(const auto&e:entries_)if(e.published||e.publishing)std::abort();refunded_=true;}
bool Ledger::ReleaseAfterRun(bool success)noexcept{if(!active_||released_||current!=this)return false;const bool valid=success?committed_:(refunded_||entries_.empty());if(!valid)std::abort();publication_stop_=nullptr;holder_=nullptr;metadata_={};entries_.clear();root_entries_.clear();graph_=nullptr;released_=true;current=previous_;return true;}
bool Install(HookEngine&hooks,void*l,std::string*error){std::lock_guard<std::mutex>lock(installation);if(installed_linker)return installed_linker==l;if(!l)return false;
 auto*target=hooks.FindSymbol("libart.so","_ZN3art11ClassLinker24RegisterExistingDexCacheENS_6ObjPtrINS_6mirror8DexCacheEEENS1_INS2_11ClassLoaderEEE");
 if(!target)return Error(error,"Missing named source DexCache publication leaf");
 if(!hooks.Install(target,reinterpret_cast<void*>(detail::Hooks::Proxy),reinterpret_cast<void**>(&original),error))return false;
 if(!original)std::abort();installed_linker=l;return true;}
}
