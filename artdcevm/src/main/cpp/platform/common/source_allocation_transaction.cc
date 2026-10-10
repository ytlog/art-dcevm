#include "platform/common/source_allocation_transaction.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/abi/arm64/common/class_redefinition_prefix.h"
#include <cstring>
#include <new>
namespace artdcevm::source_allocation_transaction {
namespace {
bool Mapped(const void* p,size_t bytes){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=bytes;}
template<class T>T Scalar(const void* p,size_t at=0){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
}
bool Read(HookEngine& hooks,const modern_field_metadata::FieldMetadata& fields,
 const transaction_root::Holder* actual,source_handle_roots::Pool& pool,Snapshot* output,std::string* error){
 using namespace abi::arm64;
 if(!output||!pool.ReadyForSourceAccess()||!fields.Calibrated()||!Mapped(actual,sizeof(*actual)))return Fail(error,"Allocation Holder lacks the independently calibrated source metadata");
 auto barrier=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 using Mirror=android11::ReleaseObjectPointer (*)(void*);
 auto mirror=reinterpret_cast<Mirror>(hooks.FindSymbol("libopenjdkjvmti.so","_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv"));
 if(!barrier||!mirror)return Fail(error,"Allocation Holder requires named source root and mirror readers");
 transaction_root::Holder holder;size_t extent=0;method_identity::ReadableMappingExtent(actual,&extent);
 if(!transaction_root::ReadHolderHeader(actual,extent,&holder)||!Mapped(holder.array_handle,4)||!Mapped(holder.redefinitions,sizeof(transaction_root::Owners)))return Fail(error,"Allocation Holder native ranges are unreadable");
 const auto& layout=fields.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 void* array=barrier(const_cast<uint32_t*>(holder.array_handle));
 if(!Mapped(array,12))return Fail(error,"Allocation Holder array root is unreadable");
 const int32_t slots=Scalar<int32_t>(array,8);
 if(slots<=0||slots%android11::kRootSlots||size_t(slots)>(SIZE_MAX-12)/4||!Mapped(array,12+size_t(slots)*4))return Fail(error,"Allocation Holder has no complete 13-slot partition");
 void* array_class=fields.Reference(array,0);
 if(!Mapped(array_class,layout.size))return Fail(error,"Allocation Holder has no native array Class");
 void* component=fields.Reference(array_class,offsetof(class_metadata::Class11,component_type));
 field_copy::CopyClassView component_identity;
 if(!Mapped(component,layout.size)||!fields.ClassIdentity(component,&component_identity,error)||!component_identity.is_object||component_identity.loader)return Fail(error,"Allocation Holder is not an actual bootstrap Object array");
 Snapshot result;const size_t count=size_t(slots)/android11::kRootSlots;
 if(!transaction_root::ReadMetadata(holder,count,transaction_root::OwnerFamily::kAndroid15,&result.metadata))return Fail(error,"Allocation owner vector or source bits disagree with the rooted partition");
 const auto& m=result.metadata;const size_t bit_bytes=8*((count+63)/64);
 if(count>SIZE_MAX/m.stride||!Mapped(m.owners.begin,count*m.stride)||!Mapped(m.initialized.begin,bit_bytes)||!Mapped(m.actually_structural.begin,bit_bytes)||!Mapped(m.initial_structural.begin,bit_bytes))return Fail(error,"Allocation metadata exceeds actual native mappings");
 try {
  result.targets.reserve(count);
  for(size_t index=0;index<count;++index){
   // Reload the source array through its named barrier for every target. Native
   // Pool allocation cannot safepoint; no managed address spans a JNI call.
   array=barrier(const_cast<uint32_t*>(holder.array_handle));
   if(!Mapped(array,12+size_t(slots)*4)||Scalar<int32_t>(array,8)!=slots)return Fail(error,"Allocation root partition changed during snapshot");
   transaction_root::Roots roots;
   if(!transaction_root::ReadRoots(m,index,[&](size_t slot){return fields.Reference(array,12+4*slot);},&roots))return Fail(error,"Allocation source target cannot be read");
   void* klass=roots.values[1];void* cache=roots.values[0];
   if(!Mapped(klass,layout.size)||!Mapped(cache,8)||mirror(roots.owner).reference!=uintptr_t(klass))return Fail(error,"Allocation target mirror disagrees with its actual source owner");
   // Both slots must exist before the allocator can publish an early capture.
   auto class_slot=pool.Add(klass,error),cache_slot=pool.Add(cache,error);
   if(!class_slot||!cache_slot)return false;
   result.targets.push_back({roots.owner,class_slot,cache_slot,roots.actually_structural});
  }
 }catch(const std::bad_alloc&){return Fail(error,"Native allocation snapshot root storage exhausted");}
 *output=std::move(result);if(error)error->clear();return true;
}
}
