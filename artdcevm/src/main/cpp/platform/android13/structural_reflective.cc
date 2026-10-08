#include "platform/android13/structural_reflective.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/jni_method_targets.h"
#include "platform/android13/transaction_metadata.h"
#include "platform/method_array_view.h"
#include <cstdlib>
#include <algorithm>
#include <cstring>
namespace artdcevm::android13 {
namespace {
using namespace structural_reflective_abi;
void (*visit)(void*,Visitor*)=nullptr;void* (*root)(void*)=nullptr;
const char* (*field_name)(void*)=nullptr;const char* (*field_type)(void*)=nullptr;
uint32_t Flags(void* member){return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(member)+4),__ATOMIC_RELAXED);}
class Update final:public Visitor {
 public:
 Update(void* mirror,const std::vector<void*>& old,const std::vector<void*>& next,const StructuralMemberMappings& maps,bool hierarchy):mirror_(mirror),old_(old),next_(next),maps_(maps),hierarchy_(hierarchy){}
 void* VisitMethod(void* method,const SourceInfo& info)override {
   if(!method)return nullptr;
   const auto source=ReadStructuralReflectionSource(info);
   if(source.type==kDexMethod && (hierarchy_||IsRetiredMethodPointer(method)||Shadows(method,true)))return nullptr;
   auto found=maps_.methods.find(method);if(found==maps_.methods.end())return method;
   void* replacement=found->second;
   if(source.type==kExecutable && !IsRetiredMethodPointer(replacement))
     __atomic_store_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(source.object)+kExecutableFlags),Flags(replacement),__ATOMIC_RELAXED);
   return source.type==kJniMethod?CanonicalizeJniMethod(replacement):replacement;
 }
 void* VisitField(void* field,const SourceInfo& info)override {
   if(!field)return nullptr;
   const auto source=ReadStructuralReflectionSource(info);
   if(source.type==kDexField && (hierarchy_||IsRetiredFieldPointer(field)||Shadows(field,false)))return nullptr;
   auto found=maps_.fields.find(field);return found==maps_.fields.end()?field:found->second;
 }
 private:
 bool Shadows(void* member,bool method) {
   const uint32_t flags=Flags(member);
   if(method){if(!(flags&(0x8|0x2)) || (flags&0x10000))return false;} // direct nonconstructor
   else if(flags&0x2)return false;
   void* declaring=root(member);
   if(!IsValidationAssignableFrom(declaring,mirror_) && std::find(old_.begin(),old_.end(),declaring)==old_.end())return false;
   for(void* candidate:next_) {
     if(method) {
       MethodStorageView storage;std::string error;
       if(!GetTransactionMetadataReader().ReadMethods(candidate,&storage,&error))std::abort();
       method_array::View view;
       const size_t bytes=storage.array?art_method_abi::kArrayDataOffset+size_t(storage.count)*sizeof(art_method_abi::ArtMethod):0;
       if(!method_array::Read(method_metadata::kAndroid12Onward,storage.array,bytes,{storage.direct_count,storage.copied_start,method_array::Order::DirectThenVirtual},&view))std::abort();
       for(uint32_t i=0;i<view.boundaries.declared_count;++i){
         if(view.IsDirect(i) && SameStructuralMethodIdentity(view.At(i),member))return true;}
     } else {
       bool statik=flags&0x8;auto view=ReadCopyClass(candidate,statik);
       for(size_t i=0;i<view.field_count;++i){auto next=ReadCopyField(candidate,statik,i);
         if(!std::strcmp(next.name,field_name(member))&&!std::strcmp(next.descriptor,field_type(member)))return true;}
     }
   }
   return false;
 }
 void* mirror_;const std::vector<void*>& old_;const std::vector<void*>& next_;const StructuralMemberMappings& maps_;bool hierarchy_;
};
}
structural_reflective_abi::SourceView ReadStructuralReflectionSource(const structural_reflective_abi::SourceInfo& info) {
 uint32_t type;std::memcpy(&type,reinterpret_cast<const unsigned char*>(&info)+8,4);void* object=nullptr;
 if(type==structural_reflective_abi::kExecutable)std::memcpy(&object,reinterpret_cast<const unsigned char*>(&info)+16,8);
 return {type,object};
}
bool ConfigureStructuralReflectiveTargets(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
 auto v=reinterpret_cast<void (*)(void*,Visitor*)>(hooks.FindSymbol("libart.so","_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE"));
 auto r=reinterpret_cast<void* (*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 auto n=reinterpret_cast<const char* (*)(void*)>(hooks.FindSymbol("libart.so","_ZN3art8ArtField7GetNameEv"));
 auto t=reinterpret_cast<const char* (*)(void*)>(hooks.FindSymbol("libart.so","_ZN3art8ArtField17GetTypeDescriptorEv"));
 if(!v||!r||!n||!t){*error="Missing named structural reflection functions";return false;}
 visit=v;root=r;field_name=n;field_type=t;return true;
}
void VisitStructuralReflectiveTargets(void* runtime,void* mirror,const std::vector<void*>& old,const std::vector<void*>& next,const StructuralMemberMappings& maps,bool hierarchy) {
 Update update(mirror,old,next,maps,hierarchy);visit(runtime,&update);
}
}
