#include "platform/common/native_schema_policy.h"
#include "platform/common/native_method_evolution_policy.h"
#include <algorithm>
namespace artdcevm::native_schema_policy {
namespace {
bool Same(const evolution::Member& a,const evolution::Member& b){return a.name==b.name&&a.descriptor==b.descriptor;}
// Fixed DEX access_flags declaration, not a list of permitted flag deltas.
// Storage/type changes retire the previous identity; unchanged typed storage
// retains bits and adopts the verified replacement declaration's access mode.
bool ValidFields(const evolution::ClassSchema& schema) {
 constexpr uint32_t kFieldFlags=0x50dfu,kVisibility=7u,kFinal=0x10u,kVolatile=0x40u;
 for(const auto& field:schema.fields){
  const auto flags=field.access,visibility=flags&kVisibility;
  if((flags&~kFieldFlags)||(visibility&&(visibility&(visibility-1)))||
     ((flags&(kFinal|kVolatile))==(kFinal|kVolatile)))return false;
  if((schema.access&0x200u)&&((flags&0x1fu)!=0x19u||(flags&0xc0u)))return false;
 }
 return true;
}
evolution::ClassSchema Normalize(evolution::ClassSchema schema){schema.access&=0xffffu;std::sort(schema.interfaces.begin(),schema.interfaces.end());for(auto& m:schema.methods)m.access=JavaFlags(m.access)|(m.name=="<init>"||m.name=="<clinit>"?0x10000u:0u);for(auto& f:schema.fields)f.access&=0xffffu;return schema;}
bool EqualMembers(const std::vector<evolution::Member>& a,const std::vector<evolution::Member>& b){return a.size()==b.size()&&std::all_of(a.begin(),a.end(),[&](const auto& m){return std::count_if(b.begin(),b.end(),[&](const auto& n){return Same(m,n)&&m.access==n.access;})==1;});}
bool Equal(const evolution::ClassSchema& a,const evolution::ClassSchema& b){return a.descriptor==b.descriptor&&a.superclass==b.superclass&&a.interfaces==b.interfaces&&a.access==b.access&&EqualMembers(a.fields,b.fields)&&EqualMembers(a.methods,b.methods);}
}
uint32_t JavaFlags(uint32_t flags){return(flags&0xffffu)|((flags&0x20000u)?0x20u:0u);}
bool Plan::Prepare(const evolution::ClassSchema& before,const evolution::ClassSchema& after,Plan* out,std::string* error){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!out)return fail("Missing native schema policy output");
 if(!ValidFields(before)||!ValidFields(after))return fail("Invalid DEX field visibility, modifier or interface declaration");
 auto old=Normalize(before),next=Normalize(after);Plan result;
 std::string local;if(!evolution::PlanMigration(old,next,&result.migration_,&local)){if(error)*error=local;return false;}
 if(old.superclass!=next.superclass||old.interfaces!=next.interfaces)
  result.requirements_.push_back({GuardKind::HierarchyGraph,{},{}});
 if(!old.interfaces.empty())result.requirements_.push_back({GuardKind::InterfaceIdentities,{},{}});
 if((old.access^next.access)&0x6200u)return fail("Cannot change Class, interface, annotation or enum role");
 const uint32_t class_mask=(next.access&0x200u)?0x3601u:0x5431u;
 const uint32_t class_difference=(old.access^next.access)&class_mask;
 // Final and abstract are independent source modifiers. A combined legal
 // transition recreates the Class once and collects every required proof.
 if((old.access&0x410u)==0x410u||(next.access&0x410u)==0x410u)return fail("A Class cannot be both final and abstract");
 if(class_difference&~0x410u)return fail("Unsupported native class modifier evolution");
 if(class_difference&&(next.access&0x10u))result.requirements_.push_back({GuardKind::LoadedSubclasses,{},{}});
 if(!(next.access&0x200u)&&!(old.access&0x400u)&&(next.access&0x400u))result.requirements_.push_back({GuardKind::ExistingInstances,{},{}});
 // Final permissions for an existing typed identity require an independent
 // write-guard owner even when visibility or volatility changes simultaneously.
 for(const auto& next_field:next.fields){
  const auto prior=std::find_if(old.fields.begin(),old.fields.end(),[&](const auto& f){return Same(f,next_field)&&f.IsStatic()==next_field.IsStatic();});
  if(prior!=old.fields.end()&&(next_field.access&0x10u)&&((prior->access^next_field.access)&0x17u))
   result.requirements_.push_back({GuardKind::FinalFields,next_field.name,next_field.descriptor});
 }
 for(const auto& next_field:next.fields){const auto prior=std::find_if(old.fields.begin(),old.fields.end(),[&](const auto& f){return Same(f,next_field);});
  if(prior!=old.fields.end()&&((prior->access^next_field.access)&0x17u))result.requirements_.push_back({GuardKind::AccessPermissions,next_field.name,next_field.descriptor});
 }
 if(!PrepareMethodEvolution(old,next,&result.requirements_,error))return false;
 *out=std::move(result);if(error)error->clear();return true;
}
bool Plan::Matches(const evolution::ClassSchema& old,const evolution::ClassSchema& next)const{return Equal(Normalize(old),migration_.before)&&Equal(Normalize(next),migration_.after);}
bool Plan::Requires(GuardKind kind)const{return std::any_of(requirements_.begin(),requirements_.end(),[&](const auto& requirement){return requirement.kind==kind;});}
bool Plan::MethodIdentity(const evolution::Member& old,const evolution::Member& next)const{return Same(old,next)&&old.IsStatic()==next.IsStatic();}

FieldEvolutionDecision CheckFieldEvolution(const std::vector<evolution::Member>& old_fields,
                                         const std::vector<evolution::Member>& new_fields) {
  constexpr uint32_t visibility = 0x0001 | 0x0002 | 0x0004;
  constexpr uint32_t final = 0x0010, volatile_flag = 0x0040;
  FieldEvolutionDecision decision;
  std::vector<bool> seen(old_fields.size(), false);
  for (const auto& field : new_fields) {
    auto old = std::find_if(old_fields.begin(), old_fields.end(), [&](const auto& candidate) {
      return candidate.name == field.name && candidate.descriptor == field.descriptor;
    });
    if (old == old_fields.end()) {
      decision.structural = true;
      continue;
    }
    const uint32_t difference = field.access ^ old->access;
    if (difference == 0) {
      seen[static_cast<size_t>(old - old_fields.begin())] = true;
      continue;
    }
    const bool visibility_change = (difference & visibility) != 0 &&
        (difference & ~(visibility | volatile_flag | final)) == 0;
    if (difference != evolution::kStatic && difference != volatile_flag &&
        difference != final && !visibility_change) {
      decision.accepted = false;
      decision.error = "Field " + field.name + ":" + field.descriptor +
          " had different access flags";
      return decision;
    }
    decision.structural = true;
    decision.final_guard |= (visibility_change || difference == final) &&
        (field.access & final) != 0;
  }
  decision.structural |= std::find(seen.begin(), seen.end(), false) != seen.end();
  return decision;
}
}
