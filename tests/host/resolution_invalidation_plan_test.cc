#include "platform/common/resolution_invalidation_plan.h"
#include "platform/common/native_structural_reflection_visitor.h"
#include <cassert>
#include <cstdio>
#include <unordered_map>
namespace art {
class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;uint32_t type=0,pad=0;void* object=nullptr;};
}
using namespace artdcevm::resolution_invalidation;
namespace {
struct Class {void* loader;Class* parent;const char* descriptor;};
struct Member {Class* owner;uint32_t flags;const char* name;const char* descriptor;};
class Proof:public Observer {
 public:
  bool fail_member=false,fail_class=false,fail_identity=false,fail_flags=false;
  mutable int member_reads=0,class_reads=0,assignability_reads=0,scope_reads=0;
  bool scope_ready=true;
  bool ValidateScope(void*)const override{++scope_reads;return scope_ready;}
  bool ReadFlags(void* pointer,Kind,uint32_t* out)const override{if(fail_flags||!pointer||!out)return false;*out=static_cast<Member*>(pointer)->flags;return true;}
  Class* interface_class=nullptr;Class* implementing_class=nullptr;
  bool ReadClass(void* pointer,ClassIdentity* out)const override {
    ++class_reads;if(fail_class||!pointer||!out)return false;auto& c=*static_cast<Class*>(pointer);
    *out={pointer,c.loader,c.parent,c.descriptor};return true;
  }
  bool ReadMember(void* pointer,Kind,MemberIdentity* out)const override {
    ++member_reads;if(fail_member||!pointer||!out)return false;auto& m=*static_cast<Member*>(pointer);
    *out={pointer,m.owner,m.flags};return true;
  }
  bool IsAssignableFrom(void* target,void* holder,bool* out)const override {
    ++assignability_reads;if(fail_class||!out)return false;*out=target==interface_class&&holder==implementing_class;return true;
  }
  bool SameIdentity(const MemberIdentity& actual,const Declaration& candidate,bool* out)const override {
    if(fail_identity||!out)return false;auto& m=*static_cast<Member*>(actual.member);
    *out=candidate.name==m.name&&candidate.descriptor==m.descriptor;return true;
  }
};
Declaration Declare(Member& m,Kind kind){Declaration d;d.member=&m;d.declaring=m.owner;d.flags=m.flags;d.kind=kind;d.name=m.name;d.descriptor=m.descriptor;return d;}
Replacement Pair(Proof& proof,Class& before,Class& after,std::vector<Declaration> members){Replacement r;assert(proof.ReadClass(&before,&r.before));assert(proof.ReadClass(&after,&r.after));r.declarations=std::move(members);return r;}
struct Cache {art::ArtMethod* method;art::ArtField* field;};
void Visit(Cache& cache,art::ReflectiveValueVisitor& visitor){art::ReflectionSourceInfo source;source.type=8;cache.method=visitor.VisitMethod(cache.method,source);source.type=9;cache.field=visitor.VisitField(cache.field,source);}
}
int main(){
 int boot_loader=0,app_loader=0,other_loader=0;
 Class base{&boot_loader,nullptr,"Lp/Base;"},old{&app_loader,&base,"Lp/Child;"},next{&app_loader,&base,"Lp/Child;"},
       unrelated{&other_loader,nullptr,"Lp/Base;"},iface{&boot_loader,nullptr,"Lp/Interface;"};
 Member ancestor_method{&base,0x9,"m","()I"},ancestor_field{&base,0x1,"f","I"},
       static_field{&base,0x9,"sf","J"},private_field{&base,0x2,"f","I"},ctor{&base,0x10001,"<init>","()V"},
       virtual_method{&base,0x1,"m","()I"},private_method{&base,0x2,"p","()V"},
       other_method{&unrelated,0x9,"m","()I"},other_field{&unrelated,0x1,"f","I"},
       overload{&base,0x9,"m","(I)I"},old_kept{&old,0x1,"f","I"},
       added_method{&next,0x9,"m","()I"},added_field{&next,0x1,"f","I"},added_static{&next,0x9,"sf","J"},
       added_private_method{&next,0x2,"p","()V"},interface_field{&iface,0x9,"sf","J"};
 auto proof=std::make_shared<Proof>();std::string error;
 const auto pair=Pair(*proof,old,next,{Declare(added_method,Kind::Method),Declare(added_field,Kind::Field),
       Declare(added_static,Kind::Field),Declare(added_private_method,Kind::Method)});
 auto plan=ResolutionInvalidationPlan::Prepare(&old,{pair},proof,&error);assert(plan&&error.empty());
 assert(plan->Inspect(&ancestor_method,Kind::Method,8)==Decision::Clear);
 assert(plan->Inspect(&ancestor_field,Kind::Field,9)==Decision::Clear);
 assert(plan->Inspect(&static_field,Kind::Field,9)==Decision::Clear);
 assert(plan->Inspect(&private_method,Kind::Method,8)==Decision::Clear); // ART direct includes private.
 assert(plan->Inspect(&private_field,Kind::Field,9)==Decision::Keep);
 assert(plan->Inspect(&ctor,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&virtual_method,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&overload,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&other_method,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&other_field,Kind::Field,9)==Decision::Keep);
 assert(plan->Inspect(&ancestor_method,Kind::Method,3)==Decision::Keep);
 assert(plan->Inspect(&ancestor_field,Kind::Field,4)==Decision::Keep);
 auto wrong_storage=pair;wrong_storage.declarations.erase(wrong_storage.declarations.begin()+2);
 Member wrong_static{&next,0x1,"sf","J"};wrong_storage.declarations.push_back(Declare(wrong_static,Kind::Field));
 auto storage=ResolutionInvalidationPlan::Prepare(&old,{wrong_storage},proof,&error);assert(storage);
 assert(storage->Inspect(&static_field,Kind::Field,9)==Decision::Keep);
 proof->interface_class=&iface;proof->implementing_class=&old;
 assert(plan->Inspect(&interface_field,Kind::Field,9)==Decision::Clear);
 proof->implementing_class=nullptr;assert(plan->Inspect(&interface_field,Kind::Field,9)==Decision::Keep);
 // One protected traversal checks the complete graph once and handles a
 // heavily prewarmed repeated member without re-decoding or OS identity work.
 proof->member_reads=proof->class_reads=proof->assignability_reads=proof->scope_reads=0;
 {
  ResolutionInvalidationPlan::Scan scan(*plan);assert(scan.Valid());const int roots=proof->class_reads;
  for(int i=0;i<10000;++i)assert(scan.Inspect(&ancestor_method,Kind::Method,8)==Decision::Clear);
  assert(proof->member_reads==1&&proof->scope_reads==1&&proof->class_reads==roots);
  assert(scan.Inspect(&overload,Kind::Method,8)==Decision::Keep);
  assert(proof->assignability_reads<=1); // Both members use the same declaring class.
 }
 auto epoch_loader=old.loader;old.loader=&other_loader;
 {ResolutionInvalidationPlan::Scan scan(*plan);assert(!scan.Valid());assert(scan.Inspect(&ancestor_method,Kind::Method,8)==Decision::Unavailable);}
 old.loader=epoch_loader;proof->scope_ready=false;
 {ResolutionInvalidationPlan::Scan scan(*plan);assert(!scan.Valid());
  assert(scan.Inspect(&virtual_method,Kind::Method,8)==Decision::Keep);
  assert(scan.Inspect(&private_field,Kind::Field,9)==Decision::Keep);
  assert(scan.Inspect(&ancestor_method,Kind::Method,8)==Decision::Unavailable);}
 proof->scope_ready=true;
 {ResolutionInvalidationPlan::Scan scan(*plan);assert(scan.Valid()&&scan.Inspect(&ancestor_method,Kind::Method,8)==Decision::Clear);}
 auto epoch_parent=old.parent;old.parent=nullptr;
 {ResolutionInvalidationPlan::Scan scan(*plan);assert(!scan.Valid());}old.parent=epoch_parent;
 auto epoch_descriptor=next.descriptor;next.descriptor="Lp/Changed;";
 {ResolutionInvalidationPlan::Scan scan(*plan);assert(!scan.Valid());}next.descriptor=epoch_descriptor;
 std::unordered_map<void*,void*> methods{{&ancestor_method,&added_method}},fields{{&old_kept,&added_field}};
 using artdcevm::native_structural_reflection::detail::Visitor;
 Visitor preflight(methods,fields,true,false,false,false,plan.get()),update(methods,fields,false,false,false,false,plan.get());
 Cache warmed{reinterpret_cast<art::ArtMethod*>(&ancestor_method),reinterpret_cast<art::ArtField*>(&ancestor_field)};
 const auto before=warmed;Visit(warmed,preflight);assert(!preflight.Unsupported()&&warmed.method==before.method&&warmed.field==before.field);
 Visit(warmed,update);assert(!warmed.method&&!warmed.field); // Actual visitor return values clear prewarmed cache slots.
 Cache untouched{reinterpret_cast<art::ArtMethod*>(&other_method),reinterpret_cast<art::ArtField*>(&other_field)};
 const auto saved=untouched;Visit(untouched,update);assert(untouched.method==saved.method&&untouched.field==saved.field);
 art::ReflectionSourceInfo info;info.type=9;
 assert(update.VisitField(reinterpret_cast<art::ArtField*>(&old_kept),info)==nullptr); // Invalidate before existing mapping.
 info.type=10;assert(update.VisitField(reinterpret_cast<art::ArtField*>(&old_kept),info)==reinterpret_cast<art::ArtField*>(&added_field));
 // Excluded entries need no declaring root/class identity, even when decoding
 // one would fail. This follows AOSP's exact eligibility-before-class order.
 proof->fail_member=true;const int before_reads=proof->member_reads;
 assert(plan->Inspect(&virtual_method,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&ctor,Kind::Method,8)==Decision::Keep);
 assert(plan->Inspect(&private_field,Kind::Field,9)==Decision::Keep);
 assert(proof->member_reads==before_reads);
 FailureInfo failure;
 assert(plan->Inspect(&ancestor_method,Kind::Method,8,&failure)==Decision::Unavailable);
 assert(failure.source_kind==8&&failure.member==&ancestor_method&&failure.flags==9&&std::string(failure.stage)=="member-identity");
 proof->fail_flags=true;assert(plan->Inspect(&ancestor_method,Kind::Method,8,&failure)==Decision::Unavailable&&std::string(failure.stage)=="access-flags");proof->fail_flags=false;
 Cache unproved=before;Visitor reject(methods,fields,true,false,false,false,plan.get());Visit(unproved,reject);
 assert(reject.Unsupported()&&reject.ResolutionUnavailable()&&reject.ResolutionFailure().source_kind==8&&std::string(reject.ResolutionFailure().stage)=="member-identity"&&unproved.method==before.method&&unproved.field==before.field);proof->fail_member=false;
 proof->fail_class=true;assert(plan->Inspect(&ancestor_field,Kind::Field,9)==Decision::Unavailable);proof->fail_class=false;
 auto saved_loader=old.loader;old.loader=&other_loader;assert(plan->Inspect(&ancestor_field,Kind::Field,9)==Decision::Unavailable);old.loader=saved_loader;
 proof->fail_identity=true;assert(plan->Inspect(&ancestor_field,Kind::Field,9)==Decision::Unavailable);proof->fail_identity=false;
 auto bad=pair;bad.after.loader=&other_loader;assert(!ResolutionInvalidationPlan::Prepare(&old,{bad},proof,&error));
 auto actual_loader=next.loader;next.loader=&other_loader;bad=Pair(*proof,old,next,pair.declarations);
 assert(!ResolutionInvalidationPlan::Prepare(&old,{bad},proof,&error));next.loader=actual_loader;
 assert(!ResolutionInvalidationPlan::Prepare(&unrelated,{pair},proof,&error));
 bad=pair;bad.declarations[0].declaring=&unrelated;assert(!ResolutionInvalidationPlan::Prepare(&old,{bad},proof,&error));
 proof->fail_class=true;assert(!ResolutionInvalidationPlan::Prepare(&old,{pair},proof,&error));proof->fail_class=false;
 auto parent=base.parent;base.parent=&old;assert(!ResolutionInvalidationPlan::Prepare(&old,{pair},proof,&error));base.parent=parent;
 assert(!ResolutionInvalidationPlan::Prepare(&old,{pair},nullptr,&error));
 std::puts("Resolution invalidation: prewarmed ancestor caches clear before mapping, exact identity/loader/ownership proofs, storage/constructor/private exclusions, interface ancestry and readonly refusal passed");
}
