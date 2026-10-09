#include "platform/common/native_method_evolution_policy.h"
#include <cassert>
#include <iostream>
using namespace artdcevm;
using namespace native_schema_policy;
int main(){
 evolution::ClassSchema before;before.descriptor="LExample;";before.access=0x401;
 before.methods={{"value","()I",1},{"<init>","()V",0x10001}};
 auto after=before;std::string error;std::vector<Requirement> requirements;
 for(uint32_t flags:{1u,2u,4u,0u,0x11u,0x21u,0x101u,0x129u,0x121u,0x401u,0x1001u}){
  after.methods[0].access=flags;requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error));
  if(flags&0x400){assert(requirements.size()==2&&requirements[0].kind==GuardKind::AbstractMethodDependents&&requirements[1].kind==GuardKind::ExistingInstances);}
  else if((flags&0x10)&&!(flags&(2|8)))assert(requirements.size()==1&&requirements[0].kind==GuardKind::FinalMethodOverrides);
  else if((flags&7u)!=1u)assert(requirements.size()==1&&requirements[0].kind==GuardKind::AccessPermissions);
  else assert(requirements.empty());
 }
 // Combined private->public final+synchronized and native->Java changes use
 // one replacement plan; incoming DEX/ART owns body and entry point verification.
 before.methods[0].access=0x102;after.methods[0].access=0x31;
 requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.size()==2&&requirements[0].kind==GuardKind::AccessPermissions&&requirements[1].kind==GuardKind::FinalMethodOverrides);
 requirements={{GuardKind::LoadedSubclasses,"sentinel","()V"}};
 const auto fail=[&](){assert(!PrepareMethodEvolution(before,after,&requirements,&error));assert(requirements.size()==1&&requirements[0].member_name=="sentinel"&&!error.empty());};
 for(uint32_t flags:{3u,0x403u,0x409u,0x411u,0x421u,0x501u,0xc01u,0x201u,0x10001u}){after.methods[0].access=flags;fail();}
 after=before;after.methods.pop_back();fail(); // Constructor retention is required.
 after=before;after.methods.push_back(after.methods[0]);fail();
 after=before;after.methods[0].descriptor="bad";fail();
 after=before;after.methods[1].access|=0x8;fail();
 before.methods.clear();after.methods={{"value","()I",0x401}};
 requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.size()==2);
 after.methods[0].access=0x11;requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.size()==1);
 // Already-final virtuals need no new override restriction; a formerly direct
 // private final promoted to public must prove loaded subclass overrides.
 before.methods=after.methods;requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.empty());
 before.methods[0].access=0x12;requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.size()==2);
 // Interfaces cannot have directly allocated instances; subtype proof remains.
 before.methods.clear();after.access=0x601;after.methods[0].access=0x401;requirements.clear();assert(PrepareMethodEvolution(before,after,&requirements,&error)&&requirements.size()==1&&requirements[0].kind==GuardKind::AbstractMethodDependents);
 std::cout<<"Method schema: legal combined modifiers, native/monitor/visibility/storage, constructors, final/abstract actual-proof requirements and failure preservation passed\n";
}
