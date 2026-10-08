#include "platform/common/resolution_invalidation_plan.h"
#include <algorithm>
#include <unordered_set>
namespace artdcevm::resolution_invalidation {
namespace {
bool SameClass(const ClassIdentity& a,const ClassIdentity& b){return a.klass==b.klass&&a.loader==b.loader&&a.parent==b.parent&&a.descriptor==b.descriptor;}
bool Direct(uint32_t flags){return (flags&(0x8u|0x2u|0x10000u))!=0;}
}
std::unique_ptr<ResolutionInvalidationPlan> ResolutionInvalidationPlan::Prepare(void* mirror,
    const std::vector<Replacement>& replacements,std::shared_ptr<const Observer> observer,std::string* error){
 const auto fail=[&](const char* text)->std::unique_ptr<ResolutionInvalidationPlan>{if(error)*error=text;return nullptr;};
 if(!mirror||replacements.empty()||!observer)return fail("Resolution invalidation lacks rooted holder/identity observer");
 auto result=std::unique_ptr<ResolutionInvalidationPlan>(new ResolutionInvalidationPlan);result->observer_=std::move(observer);result->holder_=mirror;
 std::unordered_set<void*> before_classes,after_classes;bool holder=false;
 for(const auto& pair:replacements){ClassIdentity before,after;
  if(!pair.before.klass||!pair.after.klass||pair.before.klass==pair.after.klass||
      !before_classes.insert(pair.before.klass).second||!after_classes.insert(pair.after.klass).second||
      !result->observer_->ReadClass(pair.before.klass,&before)||!result->observer_->ReadClass(pair.after.klass,&after)||
      !SameClass(before,pair.before)||!SameClass(after,pair.after)||before.descriptor.empty()||
      before.descriptor!=after.descriptor||before.loader!=after.loader)
    return fail("Resolution invalidation class/loader identity proof mismatch");
  result->classes_.push_back(before);result->classes_.push_back(after);
  holder|=before.klass==mirror;
  result->eligible_classes_.push_back(before.klass);
  for(const auto& candidate:pair.declarations){MemberIdentity actual;
   if(!candidate.member||candidate.declaring!=after.klass||candidate.name.empty()||candidate.descriptor.empty()||
       !result->observer_->ReadMember(candidate.member,candidate.kind,&actual)||
       actual.member!=candidate.member||actual.declaring!=candidate.declaring||actual.flags!=candidate.flags)
     return fail("Resolution invalidation replacement declaration lacks native ownership proof");
   bool identity=false;
   if(!result->observer_->SameIdentity(actual,candidate,&identity)||!identity)
     return fail("Resolution invalidation replacement name/descriptor proof mismatch");
   result->declarations_.push_back(candidate);
  }
 }
 if(!holder)return fail("Resolution invalidation holder mirror is not an original transaction Class");
 // Only real superclass identity links establish ancestry. Boot delegation may
 // legitimately cross loader identities; a same descriptor from another loader
 // never enters this graph. Null loader denotes the actual bootstrap loader.
 std::unordered_set<void*> ancestry;
 for(void* klass=mirror;klass;){ClassIdentity facts;
  if(!ancestry.insert(klass).second||ancestry.size()>4096||!result->observer_->ReadClass(klass,&facts)||
      facts.klass!=klass||facts.descriptor.empty())return fail("Resolution invalidation ancestor proof missing or cyclic");
  result->classes_.push_back(facts);result->eligible_classes_.push_back(klass);klass=facts.parent;
 }
 if(error)error->clear();return result;
}
Decision ResolutionInvalidationPlan::Inspect(void* member,Kind kind,uint32_t source_kind,FailureInfo* failure)const{
 if(failure)*failure={};
 if((kind==Kind::Method&&source_kind!=8)||(kind==Kind::Field&&source_kind!=9))return Decision::Keep;
 MemberIdentity actual;uint32_t flags=0;
 auto unavailable=[&](const char* stage){if(failure)*failure={source_kind,kind,member,actual.declaring,flags,stage,observer_?observer_->FailureReason():nullptr};return Decision::Unavailable;};
 if(!observer_||!member||!observer_->ReadFlags(member,kind,&flags))return unavailable("access-flags");
 // Match AOSP could_change_resolution_of's cheap checks before demanding any
 // declaring Class identity. Runtime, virtual, constructor and private-field
 // entries cannot be shadowed through this cache path.
 if(kind==Kind::Method){if(!Direct(flags)||(flags&0x10000u))return Decision::Keep;}
 else if(flags&0x2u)return Decision::Keep;
 if(!observer_->ReadMember(member,kind,&actual)||actual.member!=member||actual.flags!=flags)return unavailable("member-identity");
 if(!actual.declaring)return unavailable("declaring-class");
 for(const auto& facts:classes_){ClassIdentity current;
  if(!observer_->ReadClass(facts.klass,&current)||!SameClass(current,facts))return unavailable("transaction-class");
 }
 if(std::find(eligible_classes_.begin(),eligible_classes_.end(),actual.declaring)==eligible_classes_.end()){
  bool assignable=false;
  if(!observer_->IsAssignableFrom(actual.declaring,holder_,&assignable))return unavailable("assignability");
  if(!assignable)return Decision::Keep;
 }
 for(const auto& candidate:declarations_){
  if(candidate.kind!=kind||(kind==Kind::Method&&!Direct(candidate.flags))||
      (kind==Kind::Field&&((candidate.flags^actual.flags)&0x8u)))continue;
  bool same=false;if(!observer_->SameIdentity(actual,candidate,&same))return unavailable("name-signature");
  if(same)return Decision::Clear;
 }
 return Decision::Keep;
}
}
