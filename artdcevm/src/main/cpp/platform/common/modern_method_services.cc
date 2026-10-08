#include "platform/common/modern_method_services.h"
#include <cstdlib>
namespace artdcevm::modern_methods {
namespace {
bool Readable(const method_array::View& table) {
  if(table.stride != sizeof(method_metadata::Method12) ||
      table.boundaries.declared_count > table.count ||
      table.boundaries.direct_count > table.boundaries.declared_count) return false;
  if(!table.array) return !table.count;
  size_t extent = 0;
  if(!table.array || uintptr_t(table.array)%8 ||
      !method_identity::ReadableMappingExtent(table.array,&extent) || extent<8 ||
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
    abi::arm64::android_common::FromReflectedMethod leaf,
    const method_identity::Oracle& native,const void* binding,
    void* linker,void* old_class,const method_array::View& old,
    void* new_class,const method_array::View& replacement,Retired retired,
    std::string* error) {
  const auto fail=[&](const char* message)->std::unique_ptr<Service>{
    if(error)*error=message;return nullptr;
  };
  if(!linker||!old_class||!new_class||!retired||!Readable(old)||!Readable(replacement)||
      (old_class==new_class&&(old.array!=replacement.array||old.count!=replacement.count)))
    return fail("Modern commit methods require owned calibrated tables and services");
  if(!native_method_binding::Calibrate(context,leaf,native,binding,error))return nullptr;
  auto root=reinterpret_cast<Root>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  auto same=reinterpret_cast<Same>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"));
  auto obsolete=reinterpret_cast<Obsolete>(hooks.FindSymbol("libart.so","_ZNK3art11ClassLinker31SetEntryPointsForObsoleteMethodEPNS_9ArtMethodE"));
  if(!root||!same||!obsolete)return fail("Missing named modern structural-method service");
  auto result=std::unique_ptr<Service>(new Service);
  result->root_=root;result->same_=same;result->obsolete_=obsolete;
  result->retired_=retired;result->class_linker_=linker;
  result->old_class_=old_class;result->new_class_=new_class;
  result->old_=old;result->new_=replacement;
  if(error)error->clear();return result;
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
  std::memcpy(&binding,static_cast<unsigned char*>(source)+offsetof(method_metadata::Method12,data),8);
  std::memcpy(static_cast<unsigned char*>(target)+offsetof(method_metadata::Method12,data),&binding,8);
}
void Service::AddFlags(void* method,uint32_t flags)const{
  RequireMember(method);
  __atomic_fetch_or(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+4),flags,__ATOMIC_RELAXED);
}
void Service::SetObsoleteEntry(void* method)const{RequireMember(method);obsolete_(class_linker_,method);}
}  // namespace artdcevm::modern_methods
