#include "platform/common/native_final_write_access.h"
#include "platform/abi/arm64/art_field.h"
#include <cstdlib>
#include <limits>
namespace artdcevm::final_write_guard {
namespace {
bool AlignedLiveDeclaration(const void*value,size_t size){
 const auto address=reinterpret_cast<uintptr_t>(value);
 return value&&address%alignof(uint32_t)==0&&address<=std::numeric_limits<uintptr_t>::max()-size;
}
}
NativeAccess::NativeAccess(const modern_field_metadata::FieldMetadata&metadata,
    native_reference_decoder::Decoder&references,abi::arm64::android11::CurrentThread current,Throw raise)
 :metadata_(metadata),references_(references),current_(current),throw_(raise),
  aliases_({this,nullptr,FieldOwner,Weak,FieldOwner,MethodOwner}),
  writer_({this,Field,Method,nullptr,Raise,Equivalent,Flags}){}
std::unique_ptr<NativeAccess> NativeAccess::Create(HookEngine&hooks,
    const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&methods,
    native_reference_decoder::Decoder&references,std::string*error){
 auto current=reinterpret_cast<abi::arm64::android11::CurrentThread>(
     hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 auto raise=reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art6Thread17ThrowNewExceptionEPKcS2_"));
 if(!metadata.Calibrated()||!references.Ready()||!current||!raise||methods.identities.size()<3||
    methods.readable_extents.size()!=methods.identities.size()||
    (methods.model!=method_identity::Model::kMethod11&&methods.model!=method_identity::Model::kMethod12Onward)){
  if(error)*error="final-writer-native-access-needs-independent-metadata-and-reference-calibration";
  return nullptr;
 }
 return std::unique_ptr<NativeAccess>(new NativeAccess(metadata,references,current,raise));
}
bool NativeAccess::Field(void*opaque,void*field,uint32_t*flags,void**declaring){
 if(!opaque||!AlignedLiveDeclaration(field,sizeof(abi::arm64::Field))||!flags||!declaring)return false;
 auto&owner=*static_cast<NativeAccess*>(opaque);
 // Source callback owns the live ArtField. The fixed declaration was selected
 // through complete public field/mirror calibration, not an OS mapping lookup.
 *flags=__atomic_load_n(&static_cast<abi::arm64::Field*>(field)->access_flags,__ATOMIC_RELAXED);
 *declaring=owner.metadata_.FieldDeclaringClass(field);return *declaring!=nullptr;
}
bool NativeAccess::Method(void*opaque,void*method,void**declaring){
 if(!opaque||!AlignedLiveDeclaration(method,8)||!declaring)return false;
 *declaring=static_cast<NativeAccess*>(opaque)->metadata_.MethodDeclaringClass(method);
 return *declaring!=nullptr;
}
bool NativeAccess::Flags(void*opaque,void*method,uint32_t*flags){
 if(!opaque||!AlignedLiveDeclaration(method,8)||!flags)return false;
 *flags=method_metadata::ReadAccessFlags(method);return true;
}
void NativeAccess::Raise(void*opaque,void*self,const char*type,const char*message){
 auto&owner=*static_cast<NativeAccess*>(opaque);
 if(!self||owner.current_()!=self)std::abort();
 owner.throw_(self,type,message);
}
bool NativeAccess::Equivalent(void*opaque,void*self,void*field,void*method){
 auto&owner=*static_cast<NativeAccess*>(opaque);
 if(!self||owner.current_()!=self)return false;
 return owner.aliases_.EquivalentWriter(self,field,method);
}
bool NativeAccess::Weak(void*opaque,void*self,void*weak,void**decoded){
 return static_cast<NativeAccess*>(opaque)->references_.Weak(self,
     native_reference_decoder::Scope::Runnable,reinterpret_cast<jweak>(weak),decoded);
}
void* NativeAccess::FieldOwner(void*opaque,void*field){
 return field?static_cast<NativeAccess*>(opaque)->metadata_.FieldDeclaringClass(field):nullptr;
}
void* NativeAccess::MethodOwner(void*opaque,void*method){
 return method?static_cast<NativeAccess*>(opaque)->metadata_.MethodDeclaringClass(method):nullptr;
}
}
