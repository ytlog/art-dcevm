#include "platform/common/native_method_evolution_policy.h"
#include <algorithm>
namespace artdcevm::native_schema_policy {
namespace {
constexpr uint32_t kVisibility=7,kPrivate=2,kStatic=8,kFinal=0x10,
    kMonitor=0x20,kNative=0x100,kAbstract=0x400,kStrict=0x800,
    kConstructor=0x10000,kMethodFlags=0x11dff;
bool Same(const evolution::Member& a,const evolution::Member& b){
  return a.name==b.name&&a.descriptor==b.descriptor;
}
bool Constructor(const evolution::Member& m){return m.name=="<init>"||m.name=="<clinit>";}
bool Valid(const evolution::ClassSchema& owner,const evolution::Member& method){
  const uint32_t f=method.access,visibility=f&kVisibility;
  if(method.name.empty()||!evolution::IsMethodDescriptor(method.descriptor)||
     (f&~kMethodFlags)||(visibility&&(visibility&(visibility-1))))return false;
  if(method.name=="<init>")return !(f&(kStatic|kFinal|kMonitor|kNative|kAbstract))&&method.descriptor.back()=='V';
  if(method.name=="<clinit>")return (f&kStatic)&&method.descriptor=="()V"&&!(f&(kVisibility|kFinal|kMonitor|kNative|kAbstract));
  if(f&kConstructor)return false;
  if(f&kAbstract)return (owner.access&(0x200u|kAbstract))&&!(f&(kPrivate|kStatic|kFinal|kMonitor|kNative|kStrict));
  return true;
}
bool Direct(const evolution::Member& method){return (method.access&(kPrivate|kStatic))||Constructor(method);}
}
bool PrepareMethodEvolution(const evolution::ClassSchema& before,
    const evolution::ClassSchema& after,std::vector<Requirement>* out,std::string* error){
  const auto fail=[&](const char* message){if(error)*error=message;return false;};
  if(!out)return fail("Missing method evolution requirement output");
  for(const auto* schema:{&before,&after})for(const auto& method:schema->methods){
    if(!Valid(*schema,method))return fail("Invalid method flags, constructor or abstract declaration");
    if(std::count_if(schema->methods.begin(),schema->methods.end(),[&](const auto& other){return Same(method,other);})!=1)
      return fail("Duplicate method name and complete descriptor");
  }
  for(const auto& old:before.methods)if(Constructor(old)){
    const auto next=std::find_if(after.methods.begin(),after.methods.end(),[&](const auto& method){return Same(old,method);});
    if(next==after.methods.end()||old.IsStatic()!=next->IsStatic())
      return fail("Constructor or class initializer retirement lacks dispatch proof");
  }
  std::vector<Requirement> pending;
  for(const auto& method:after.methods){
    const auto old=std::find_if(before.methods.begin(),before.methods.end(),[&](const auto& prior){return Same(prior,method);});
    // Added virtual declarations can override inherited slots. Their loaded
    // subclasses need the same final/abstract proof as an existing declaration.
    const bool existed=old!=before.methods.end();
    if(existed&&((old->access^method.access)&kVisibility))
      pending.push_back({GuardKind::AccessPermissions,method.name,method.descriptor});
    if(!Direct(method)&&(method.access&kFinal)&&(!existed||!(old->access&kFinal)||Direct(*old)))
      pending.push_back({GuardKind::FinalMethodOverrides,method.name,method.descriptor});
    if(!Direct(method)&&(method.access&kAbstract)&&(!existed||!(old->access&kAbstract)||Direct(*old))){
      pending.push_back({GuardKind::AbstractMethodDependents,method.name,method.descriptor});
      if(!(after.access&0x200u))pending.push_back({GuardKind::ExistingInstances,method.name,method.descriptor});
    }
    // All valid modifier combinations recreate the verified replacement class.
    // ART's new method/vtable/entry points select private/static/native/monitor
    // semantics; same-static cached identities map to the new ArtMethod. A
    // staticness change is retirement+addition, never native binding transfer.
  }
  auto result=*out;result.insert(result.end(),pending.begin(),pending.end());
  *out=std::move(result);if(error)error->clear();return true;
}
}
