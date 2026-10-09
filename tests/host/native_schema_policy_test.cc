#include "platform/common/native_schema_policy.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace artdcevm;
using native_schema_policy::Plan;
using native_schema_policy::GuardKind;
int main(){
 evolution::ClassSchema before;before.descriptor="LExample;";before.superclass="Ljava/lang/Object;";before.access=1;
 before.fields={{"keep","I",1},{"typed","I",1},{"storage","I",1},{"remove","Ljava/lang/Object;",1},{"finalized","I",1}};
 before.methods={{"<init>","()V",1},{"run","()I",1}};
 auto after=before;after.fields={{"keep","I",0x41},{"typed","J",1},{"storage","I",9},{"fresh","Ljava/lang/Object;",1},{"finalized","I",0x11}};
 after.methods.push_back({"added","()I",1});Plan plan;std::string error;
 assert(Plan::Prepare(before,after,&plan,&error));
 const auto& migration=plan.Migration();
 assert((migration.required_features&(evolution::kAddField|evolution::kRemoveField|evolution::kChangeFieldType|evolution::kChangeStorage|evolution::kChangeModifiers|evolution::kAddMethod))==(evolution::kAddField|evolution::kRemoveField|evolution::kChangeFieldType|evolution::kChangeStorage|evolution::kChangeModifiers|evolution::kAddMethod));
 unsigned retained=0,retired=0,initialized=0;
 for(const auto& field:migration.fields){retained+=field.action==evolution::MemberAction::kRetain;retired+=field.action==evolution::MemberAction::kRetire;initialized+=field.action==evolution::MemberAction::kInitialize;}
 assert(retained==2&&retired==3&&initialized==3);
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& requirement){return requirement.kind==GuardKind::FinalFields;}));
 assert(plan.Matches(before,after));auto reordered=after;std::reverse(reordered.fields.begin(),reordered.fields.end());assert(plan.Matches(before,reordered));
 reordered.fields[0].access^=0x40;assert(!plan.Matches(before,reordered));
 assert(!plan.MethodIdentity(before.methods[1],{"run","()I",9}));
 assert(plan.MethodIdentity(before.methods[1],{"run","()I",0x21}));
 auto invalid=after;invalid.fields.push_back(invalid.fields[0]);assert(!Plan::Prepare(before,invalid,&plan,&error));
 invalid=after;invalid.superclass="LDifferent;";assert(Plan::Prepare(before,invalid,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::HierarchyGraph;}));
 invalid=after;invalid.interfaces={"LNewInterface;"};assert(Plan::Prepare(before,invalid,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::HierarchyGraph;}));
 invalid=after;invalid.methods.erase(invalid.methods.begin());assert(!Plan::Prepare(before,invalid,&plan,&error));
 invalid=after;invalid.methods[0].access|=8;assert(!Plan::Prepare(before,invalid,&plan,&error));
 invalid=after;invalid.fields[0].access|=0x100;assert(!Plan::Prepare(before,invalid,&plan,&error));
 // Combined legal transitions use identity semantics rather than a one-bit
 // delta whitelist. Final+volatile and multiple visibility bits remain invalid.
 auto combined=before;combined.fields[0].access=0x10cau; // private static volatile transient synthetic
 assert(Plan::Prepare(before,combined,&plan,&error));
 const auto& combined_migration=plan.Migration();
 assert(std::count_if(combined_migration.fields.begin(),combined_migration.fields.end(),[](const auto& m){return m.action==evolution::MemberAction::kRetire;})==1);
 assert(std::count_if(combined_migration.fields.begin(),combined_migration.fields.end(),[](const auto& m){return m.action==evolution::MemberAction::kInitialize;})==1);
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::AccessPermissions;}));
 combined=before;combined.fields[0].access=0x1012u; // private final synthetic, same storage
 assert(Plan::Prepare(before,combined,&plan,&error));
 assert(std::all_of(plan.Migration().fields.begin(),plan.Migration().fields.end(),[](const auto& m){return m.action==evolution::MemberAction::kRetain;}));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::FinalFields;}));
 combined.fields[0].access|=0x40u;assert(!Plan::Prepare(before,combined,&plan,&error));
 combined.fields[0].access=3u;assert(!Plan::Prepare(before,combined,&plan,&error));
 combined.fields[0].access=0x10001u;assert(!Plan::Prepare(before,combined,&plan,&error));
 auto interface_schema=before;interface_schema.access=0x601u;interface_schema.methods.clear();
 for(auto& field:interface_schema.fields)field.access=0x19u;
 assert(Plan::Prepare(interface_schema,interface_schema,&plan,&error));
 combined=interface_schema;combined.fields[0].access=0x11u;assert(!Plan::Prepare(interface_schema,combined,&plan,&error));
 auto abstract_class=before;abstract_class.access|=0x400;assert(Plan::Prepare(before,abstract_class,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& requirement){return requirement.kind==GuardKind::ExistingInstances;}));
 auto final_class=before;final_class.access|=0x10;assert(Plan::Prepare(before,final_class,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& requirement){return requirement.kind==GuardKind::LoadedSubclasses;}));
 auto final_to_abstract=final_class;final_to_abstract.access=(final_to_abstract.access&~0x10u)|0x400u;
 assert(Plan::Prepare(final_class,final_to_abstract,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::ExistingInstances;}));
 assert(std::none_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::LoadedSubclasses;}));
 assert(Plan::Prepare(abstract_class,final_class,&plan,&error));
 assert(std::any_of(plan.Requirements().begin(),plan.Requirements().end(),[](const auto& r){return r.kind==GuardKind::LoadedSubclasses;}));
 auto invalid_class=before;invalid_class.access|=0x410u;
 assert(!Plan::Prepare(before,invalid_class,&plan,&error));
 assert(!Plan::Prepare(invalid_class,before,&plan,&error));
 invalid_class=before;invalid_class.access|=0x200u;
 assert(!Plan::Prepare(before,invalid_class,&plan,&error));
 std::cout<<"Native schema policy: typed retain/retire/initialize, field storage/modifiers, full immutable identity, constructor stability and required live-Class/instance proofs passed\n";
}
