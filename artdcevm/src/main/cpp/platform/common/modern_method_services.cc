#include "platform/common/modern_method_services.h"
#include "platform/common/class_metadata_abi.h"
#include <cstdlib>
#include "platform/common/modern_field_metadata.h"
namespace artdcevm::modern_methods {
namespace {
bool Readable(const method_array::View& table,const method_metadata::Layout& layout,const modern_field_metadata::ReadContext* context) {
  if(table.stride != layout.size ||
      table.boundaries.declared_count > table.count ||
      table.boundaries.direct_count > table.boundaries.declared_count) return false;
  if(!table.array) return !table.count;
  size_t extent = 0;
  if(!table.array || uintptr_t(table.array)%8 ||
      !modern_field_metadata::ReadMappingExtent(context,table.array,&extent) || extent<8 ||
      table.count > (extent-8)/table.stride) return false;
  uint32_t count;
  std::memcpy(&count,table.array,4);
  return count == table.count;
}
bool Contains(const method_array::View& table,void* method) {
  if(!method || !table.array) return false;
  const auto begin=uintptr_t(table.array)+8, address=uintptr_t(method);
  return address>=begin && (address-begin)%table.stride==0 &&
      (address-begin)/table.stride<table.count;
}
}
std::unique_ptr<Service> Service::Prepare(HookEngine& hooks,
    const runnable_jni::Context& context,
    abi::common::FromReflectedMethod leaf,
    const method_identity::Oracle& native,const void* binding,
    void* linker,void* old_class,const method_array::View& old,
    void* new_class,const method_array::View& replacement,Retired retired,
    std::string* error) {
  auto binding_capability=BindOwned(hooks,context,leaf,native,binding,linker,error);
  return binding_capability?PrepareTables(*binding_capability,old_class,old,
      new_class,replacement,retired,error):nullptr;
}
std::unique_ptr<Binding> Service::BindOwned(HookEngine& hooks,
    const runnable_jni::Context& context,
    abi::common::FromReflectedMethod leaf,
    const method_identity::Oracle& native,const void* binding,
    void* linker,std::string* error) {
  return BindOwned(hooks,context,leaf,native,binding,linker,
      method_identity::Model::kMethod12Onward,error);
}
std::unique_ptr<Binding> Service::BindOwned(HookEngine& hooks,
    const runnable_jni::Context& context,
    abi::common::FromReflectedMethod leaf,
    const method_identity::Oracle& native,const void* binding,
    void* linker,method_identity::Model model,std::string* error) {
  const method_metadata::Layout* layout=nullptr;
  switch(model) {
    case method_identity::Model::kMethod11:layout=&method_metadata::kAndroid11;break;
    case method_identity::Model::kMethod12Onward:layout=&method_metadata::kAndroid12Onward;break;
    default:if(error)*error="No independently calibrated commit method model";return nullptr;
  }
  if(!linker){if(error)*error="Missing observed ClassLinker receiver";return nullptr;}
  if(!native_method_binding::Calibrate(context,leaf,native,binding,model,error))return nullptr;
  auto root=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto same=reinterpret_cast<Same>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
  auto obsolete=reinterpret_cast<Obsolete>(hooks.FindSymbol("libart.so","_ZNK3art11ClassLinker31SetEntryPointsForObsoleteMethodEPNS_9ArtMethodE"));
  if(!root||!same||!obsolete){if(error)*error="Missing named modern structural-method service";return nullptr;}
  auto result=std::unique_ptr<Binding>(new Binding);
  result->root_=root;result->same_=same;result->obsolete_=obsolete;result->class_linker_=linker;result->layout_=layout;
  if(error)error->clear();return result;
}
std::unique_ptr<Service> Service::PrepareTables(const Binding& binding,
    void* old_class,const method_array::View& old,
    void* new_class,const method_array::View& replacement,Retired retired,
    std::string* error,const modern_field_metadata::ReadContext* context) {
  const auto fail=[&](const char* message)->std::unique_ptr<Service>{
    if(error)*error=message;return nullptr;
  };
  if(!binding.layout_||!binding.root_||!binding.same_||!binding.obsolete_||!binding.class_linker_||
      !old_class||!new_class||!retired||!Readable(old,*binding.layout_,context)||!Readable(replacement,*binding.layout_,context)||
      (old_class==new_class&&(old.array!=replacement.array||old.count!=replacement.count)))
    return fail("Modern commit methods require owned calibrated tables and services");
  for(const auto& entry:{std::pair<void*,const method_array::View*>{old_class,&old},
                         std::pair<void*,const method_array::View*>{new_class,&replacement}}) {
    for(uint32_t i=0;i<entry.second->count;++i) {
      void* method=entry.second->At(i);uint32_t compressed=0;
      std::memcpy(&compressed,method,4);
      void* observed=binding.root_(method);
      if(!observed||uintptr_t(observed)!=compressed)
        return fail("Method declaring-class root encoding disagrees with named barrier");
      if(i<entry.second->boundaries.declared_count&&observed!=entry.first)
        return fail("Declared method is not owned by its rooted class");
    }
  }
  auto result=std::unique_ptr<Service>(new Service);
  result->root_=binding.root_;result->same_=binding.same_;result->obsolete_=binding.obsolete_;
  result->retired_=retired;result->class_linker_=binding.class_linker_;
  result->old_class_=old_class;result->new_class_=new_class;
  result->old_=old;result->new_=replacement;result->layout_=binding.layout_;result->read_context_=context;
  if(error)error->clear();return result;
}
void Service::RestoreOldDeclaringClasses()const {
  if(uintptr_t(old_class_)>UINT32_MAX)std::abort();
  const uint32_t owner=uint32_t(uintptr_t(old_class_));
  for(uint32_t i=0;i<old_.count;++i) {
    void* method=old_.At(i);const auto flags=Flags(method);
    // Fixed ArtMethod::IsCopied excludes conflicting default methods.
    if((flags&0x1000000u)&&!(flags&0x80000000u))continue;
    std::memcpy(method,&owner,sizeof(owner));
  }
}
bool Service::MatchesClassTable(void* klass)const noexcept{
 static_assert(offsetof(class_metadata::Class11,methods)==offsetof(class_metadata::Class17,methods));
 if(klass!=old_class_&&klass!=new_class_)return false;
 if(read_context_){size_t class_extent=0;
  if(!modern_field_metadata::ReadMappingExtent(read_context_,klass,&class_extent)||
     class_extent<offsetof(class_metadata::Class11,methods)+sizeof(uint64_t))return false;
 }
 const auto& expected=Table(klass);
 uint64_t pointer=0;std::memcpy(&pointer,static_cast<const unsigned char*>(klass)+offsetof(class_metadata::Class11,methods),8);
 void* array=reinterpret_cast<void*>(uintptr_t(pointer));size_t extent=0;
 if(array&&!modern_field_metadata::ReadMappingExtent(read_context_,array,&extent))return false;
 method_array::View actual;
 if(!method_array::Read(*layout_,array,extent,expected.boundaries,&actual)||actual.array!=expected.array||actual.count!=expected.count)return false;
 for(uint32_t i=0;i<actual.count;++i)if(actual.At(i)!=expected.At(i))return false;
 return true;
}
const method_array::View& Service::Table(void* klass)const {
  if(klass==old_class_)return old_;
  if(klass==new_class_)return new_;
  std::abort();
}
void Service::RequireMember(void* method)const {
  if(!Contains(old_,method)&&!Contains(new_,method))std::abort();
}
size_t Service::Count(void* klass)const{return Table(klass).count;}
void* Service::At(void* klass,size_t index)const{
  const auto& table=Table(klass);if(index>=table.count)std::abort();
  return table.At(static_cast<uint32_t>(index));
}
uint32_t Service::Flags(void* method)const{RequireMember(method);return method_metadata::ReadAccessFlags(method);}
void* Service::DeclaringClass(void* method)const{RequireMember(method);return root_(method);}
bool Service::SameIdentity(void* a,void* b)const{RequireMember(a);RequireMember(b);return same_(a,b);}
bool Service::IsRetired(void* method)const{RequireMember(method);return retired_(method);}
void Service::TransferNativeBinding(void* source,void* target)const{
  RequireMember(source);RequireMember(target);
  if(!(Flags(source)&0x100u)||!(Flags(target)&0x100u))std::abort();
  uint64_t binding;
  std::memcpy(&binding,static_cast<unsigned char*>(source)+layout_->data,8);
  std::memcpy(static_cast<unsigned char*>(target)+layout_->data,&binding,8);
}
void Service::AddFlags(void* method,uint32_t flags)const{
  RequireMember(method);
  __atomic_fetch_or(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4),flags,__ATOMIC_RELAXED);
}
void Service::SetObsoleteEntry(void* method)const{RequireMember(method);obsolete_(class_linker_,method);}
}  // namespace artdcevm::modern_methods
