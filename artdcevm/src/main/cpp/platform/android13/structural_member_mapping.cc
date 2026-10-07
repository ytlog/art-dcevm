#include "platform/android13/structural_member_mapping.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/field_copy_metadata.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/retired_method_registry_bridge.h"
#include "platform/android13/nterp_dispatch.h"
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
bool (*same)(void*,void*)=nullptr;
bool (*profile)(void*)=nullptr;
uint32_t Flags(void* member) { return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(member)+4),__ATOMIC_RELAXED); }
void* Methods(void* klass) { uint64_t value;std::memcpy(&value,static_cast<unsigned char*>(klass)+offsetof(validation_abi::ClassPrefix,methods),8);return reinterpret_cast<void*>(value); }
uint32_t Count(void* array) { uint32_t value=0;if(array)std::memcpy(&value,array,4);return value; }
void* At(void* array,uint32_t i) {return static_cast<unsigned char*>(array)+art_method_abi::kArrayDataOffset+sizeof(art_method_abi::ArtMethod)*i;}
}
bool ConfigureStructuralMemberMappings(HookEngine& hooks,void* implementation,std::string* error) {
  profile=reinterpret_cast<bool (*)(void*)>(dlsym(implementation,"ArtDcevmSourceProbeStructuralMemberMappings"));
  same=reinterpret_cast<bool (*)(void*,void*)>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
  if(!same||!profile){*error="Missing named ART method identity function";return false;}return true;
}
bool SameStructuralMethodIdentity(void* a,void* b) {return same(a,b);}
StructuralMemberMappings BuildStructuralMemberMappings(const std::vector<void*>& old_classes,const std::vector<void*>& new_classes) {
  if(old_classes.size()!=new_classes.size())std::abort();
  StructuralMemberMappings result;
  for(size_t pair=0;pair<old_classes.size();++pair) {
    if(!profile(old_classes[pair])||!profile(new_classes[pair]))std::abort();
    for(bool statik:{true,false}) {
      auto old_view=ReadCopyClass(old_classes[pair],statik),new_view=ReadCopyClass(new_classes[pair],statik);
      for(size_t i=0;i<old_view.field_count;++i) {
        auto old=ReadCopyField(old_classes[pair],statik,i);void* replacement=nullptr;
        for(size_t j=0;j<new_view.field_count;++j){auto next=ReadCopyField(new_classes[pair],statik,j);
          if(!std::strcmp(old.name,next.name)&&!std::strcmp(old.descriptor,next.descriptor)){replacement=next.field;break;}}
        if(!replacement){RetireFieldPointer(old.field);replacement=old.field;}result.fields[old.field]=replacement;
      }
    }
    void* old_array=Methods(old_classes[pair]);void* new_array=Methods(new_classes[pair]);
    for(uint32_t i=0;i<Count(old_array);++i) {
      void* old=At(old_array,i),*replacement=nullptr;bool static_changed=false;
      for(uint32_t j=0;j<Count(new_array);++j){void* next=At(new_array,j);
        if(!same(next,old))continue;
        if(((Flags(next)^Flags(old))&0x8)==0){replacement=next;break;}static_changed=true;}
      if(!replacement){if(static_changed)RequireDispatchChecks();RetireMethodPointer(old);replacement=old;}
      else if((Flags(old)^Flags(replacement))&0x7)RequireDispatchChecks();
      result.methods[old]=replacement;
    }
  }
  return result;
}
}
