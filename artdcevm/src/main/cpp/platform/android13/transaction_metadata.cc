#include "platform/android13/transaction_metadata.h"
#include "platform/android13/heap_reference.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/common/method_array_view.h"
#include "platform/common/native_cache_view.h"
#include "platform/common/type_cache_update.h"
#include "platform/common/transaction_root_view.h"
#include <cstring>
#include <limits>
namespace artdcevm::android13 {
namespace {
TransactionMetadataReader reader;
using Class=validation_abi::ClassPrefix;
using Method=art_method_abi::ArtMethod;
using Holder=allocation_abi::Holder;
template<class T> T Read(const void* p,size_t at=0){T value;std::memcpy(&value,static_cast<const unsigned char*>(p)+at,sizeof(value));return value;}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool Aligned(const void* p,size_t alignment){return p && !(uintptr_t(p)&(alignment-1));}
uint32_t Flags(void* method){return __atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(method)+offsetof(Method,access_flags)),__ATOMIC_RELAXED);}
int32_t Length(void* a){return Read<int32_t>(a,allocation_abi::kArrayLengthOffset);}
void* Element(void* array,size_t index){return ReadHeapReference(array,allocation_abi::kReferenceArrayDataOffset+4*index);}
bool ReferenceArray(void* array){
 if(!array||Length(array)<0)return false;
 auto* type=ReadHeapReference(array,0);if(!type)return false;
 auto* component=ReadHeapReference(type,offsetof(Class,component_type));
 return component && !(Read<uint32_t>(component,offsetof(Class,primitive_type))&0xffff);
}
bool ClassArray(void* array,void* meta){return ReferenceArray(array)&&ReadHeapReference(ReadHeapReference(array,0),offsetof(Class,component_type))==meta;}
}
TransactionMetadataReader& GetTransactionMetadataReader(){return reader;}
bool ConfigureTransactionMetadataReader(HookEngine& hooks,const DeviceProfile& profile,std::string* error){return reader.Configure(hooks,profile,error);}
bool TransactionMetadataReader::Configure(HookEngine& hooks,const DeviceProfile& profile,std::string* error){
 root_=nullptr;
 if(!profile.Matched())return Fail(error,"Transaction metadata requires matched device declarations");
 auto root=reinterpret_cast<decltype(root_)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 if(!root)return Fail(error,"Missing named ART transaction member root reader");
 root_=root;return true;
}
bool TransactionMetadataReader::ReadHolder(const void* raw,TransactionHolderView* output,std::string* error)const{
 if(!root_||!Aligned(raw,alignof(Holder))||!output)return Fail(error,"Invalid transaction holder context");
 Holder holder;
 if(!transaction_root::ReadHolderHeader(raw,sizeof(Holder),&holder))
  return Fail(error,"Missing transaction holder roots or owners");
 auto* array=reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(holder.array_handle)));
 if(!ReferenceArray(array))return Fail(error,"Transaction holder is not a reference array");
 const int32_t slots=Length(array);
 if(slots%allocation_abi::kSlots)return Fail(error,"Transaction root slot count is inconsistent");
 const size_t count=size_t(slots/allocation_abi::kSlots);
 transaction_root::Metadata metadata;
 if(!transaction_root::ReadMetadata(holder,count,transaction_root::OwnerFamily::kAndroid13,&metadata))
  return Fail(error,"Transaction native owner or structural bitvector range is inconsistent");
 const auto& owners=metadata.owners;
 *output={raw,array,count,{owners.begin,owners.end,owners.capacity},metadata.initialized,
          metadata.actually_structural,metadata.initial_structural};return true;
}
bool TransactionMetadataReader::ReadRoots(const void* raw,TransactionRootView* output,std::string* error)const{
 if(!root_||!Aligned(raw,alignof(allocation_abi::Iterator))||!output)return Fail(error,"Invalid transaction iterator context");
 allocation_abi::Iterator iterator;TransactionHolderView holder;
 if(!transaction_root::ReadIterator(raw,sizeof(iterator),&iterator) || !ReadHolder(iterator.holder,&holder,error))
  return Fail(error,"Invalid transaction iterator holder");
 transaction_root::Metadata metadata{holder.count,sizeof(redefinition_abi::ClassRedefinition),
     {holder.owners.begin,holder.owners.end,holder.owners.capacity},holder.initialized,
     holder.actually_structural,holder.initial_structural};
 transaction_root::Roots roots;
 if(!transaction_root::ReadRoots(metadata,size_t(iterator.index),
     [&](size_t slot){return Element(holder.array,slot);},&roots))
  return Fail(error,"Transaction iterator is outside root slots");
 *output={holder,roots.index,roots.owner,roots.initialized,roots.actually_structural,roots.initial_structural,
     roots.values[0],roots.values[1],roots.values[2],roots.values[3],roots.values[4],roots.values[5]};
 // Allocation stages may legitimately not have materialized the later arrays.
 // Commit and allocation validation impose their own phase-specific presence.
 return true;
}
bool TransactionMetadataReader::ReadCache(void* raw,hierarchy_allocation_abi::Cache* output,std::string* error)const{
 if(!root_||!output)return Fail(error,"Invalid transaction DexCache context");
 native_cache::View fields,methods;type_cache::View types;
 if(!native_cache::Read(dex_cache_metadata::Model::Pairs,raw,sizeof(hierarchy_allocation_abi::Cache),native_cache::Member::Fields,0,&fields) ||
    !native_cache::Read(dex_cache_metadata::Model::Pairs,raw,sizeof(hierarchy_allocation_abi::Cache),native_cache::Member::Methods,0,&methods) ||
    !type_cache::Read(dex_cache_metadata::Model::Pairs,raw,sizeof(hierarchy_allocation_abi::Cache),0,&types))
  return Fail(error,"Transaction DexCache array range is inconsistent");
 const auto cache=Read<hierarchy_allocation_abi::Cache>(raw);
 if(!cache.prefix.dex_file || (!cache.types && cache.num_types))
  return Fail(error,"Transaction DexCache native link or type storage is missing");
 *output=cache;return true;
}
bool TransactionMetadataReader::ReadMethods(void* klass,MethodStorageView* output,std::string* error)const{
 if(!root_||!klass||!output)return Fail(error,"Missing method storage class");
 method_array::View view;
 if(!method_array::ReadTrustedClass(method_array::kLegacy,method_metadata::kAndroid12Onward,klass,sizeof(Class),method_array::Kind::Ordinary,nullptr,&view))
  return Fail(error,"Method storage class, range or partitions are inconsistent");
 const uint32_t count=view.count;
 const auto& boundaries=view.boundaries;
 for(uint32_t i=0;i<count;++i){
  auto* method=view.At(i);
  auto* declaring=root_(method);if(!declaring)return Fail(error,"Method storage member lacks a declaring class");
  const uint32_t flags=Flags(method);const bool is_copied=(flags&(0x1000000|0x80000000))==0x1000000;
  if(declaring!=klass&&(!is_copied||!(Read<uint32_t>(declaring,offsetof(Class,access_flags))&0x200)))return Fail(error,"Method storage member has an invalid declaring class");
 }
 *output={view.array,count,static_cast<uint16_t>(boundaries.direct_count),static_cast<uint16_t>(boundaries.declared_count)};return true;
}
bool TransactionMetadataReader::ReadAbstractVTable(void* klass,bool* has_abstract,std::string* error)const{
 if(!root_||!klass||!has_abstract)return Fail(error,"Missing abstract class metadata");
 *has_abstract=false;if(!(Read<uint32_t>(klass,offsetof(Class,access_flags))&allocation_abi::kAbstractFlag))return true;
 // SOURCE ShouldHaveEmbeddedVTable() is IsInstantiable(): even abstract arrays
 // have embedded tables. Redefinable ordinary abstract classes must not.
 if(ReadHeapReference(klass,offsetof(Class,component_type)))return Fail(error,"Abstract replacement unexpectedly has an embedded vtable");
 auto* table=ReadHeapReference(klass,offsetof(Class,vtable));if(!table)return true;
 auto* table_type=ReadHeapReference(table,0);
 auto* component=table_type?ReadHeapReference(table_type,offsetof(Class,component_type)):nullptr;
 if(!component || (Read<uint32_t>(component,offsetof(Class,primitive_type))&0xffff)!=6)return Fail(error,"External vtable is not a 64-bit pointer array");
 if(Length(table)<0)return Fail(error,"External vtable has a negative length");
 for(int32_t i=0;i<Length(table);++i){auto* method=reinterpret_cast<void*>(Read<uint64_t>(table,allocation_abi::kPointerArrayDataOffset+8*size_t(i)));
  if(!Aligned(method,alignof(Method)))return Fail(error,"External vtable has an invalid method entry");
  *has_abstract|=(Flags(method)&allocation_abi::kAbstractFlag)!=0;
 }
 return true;
}
bool TransactionMetadataReader::CheckCommitTypes(const TransactionRootView& roots,std::string* error)const{
 if(!root_||!roots.mirror)return Fail(error,"Missing commit mirror root");
 auto* meta=ReadHeapReference(roots.mirror,0);if(!meta)return Fail(error,"Missing commit class metatype");
 if(!ClassArray(roots.old_classes,meta)||!ClassArray(roots.new_classes,meta)||
    !ReferenceArray(roots.old_instances)||!ReferenceArray(roots.new_instances))return Fail(error,"Commit roots have incompatible array types");
 const int32_t count=Length(roots.old_classes),instances=Length(roots.old_instances);
 if(count!=Length(roots.new_classes)||instances!=Length(roots.new_instances))return Fail(error,"Commit root array lengths disagree");
 for(int32_t i=0;i<count;++i){auto* old=Element(roots.old_classes,i);auto* next=Element(roots.new_classes,i);
  if(!old||!next||ReadHeapReference(old,0)!=meta||ReadHeapReference(next,0)!=meta)return Fail(error,"Commit class mapping contains a non-class root");
  class_metadata::State state;
  if(!class_metadata::ReadState(class_metadata::kLegacy,old,sizeof(Class),&state))return Fail(error,"Invalid commit class state");
  const uint32_t status=state.status;
  if(status==2||status==3)return Fail(error,"Commit source class is erroneous");
 }
 for(int32_t i=0;i<instances;++i){auto* old=Element(roots.old_instances,i);auto* next=Element(roots.new_instances,i);
  if(!old||!next)return Fail(error,"Commit instance mapping contains a null root");
  auto* type=ReadHeapReference(old,0);bool found=false;
  for(int32_t j=0;j<count;++j)if(type==Element(roots.old_classes,j)){
   if(ReadHeapReference(next,0)!=Element(roots.new_classes,j))return Fail(error,"Commit instance replacement has a different target class");found=true;break;
  }
  if(!found)return Fail(error,"Commit instance has no class replacement mapping");
 }
 return true;
}
}
