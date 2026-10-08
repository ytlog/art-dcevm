#include "platform/android13/transaction_metadata.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/redefinition_art_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/method_array_view.h"
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
bool BitsValid(const allocation_abi::BitVector& bits,size_t count){
 // Platform vector<bool>::capacity stores words, while size stores bits.
 const size_t words=count/64+(count%64!=0);
 return bits.size==count&&bits.capacity>=words&&
     (bits.begin ? Aligned(bits.begin,alignof(uint64_t))&&
         bits.capacity<=(std::numeric_limits<uintptr_t>::max()-uintptr_t(bits.begin))/8 : !count&&!bits.capacity);
}
bool Bit(const allocation_abi::BitVector& bits,size_t index){return (Read<uint64_t>(bits.begin,8*(index/64))&(uint64_t{1}<<(index%64)))!=0;}
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
 const auto holder=Read<Holder>(raw);
 if(!Aligned(holder.array_handle,alignof(uint32_t))||!Aligned(holder.redefinitions,alignof(hierarchy_allocation_abi::Vector)))return Fail(error,"Missing transaction holder roots or owners");
 auto* array=reinterpret_cast<void*>(uintptr_t(Read<uint32_t>(holder.array_handle)));
 if(!ReferenceArray(array))return Fail(error,"Transaction holder is not a reference array");
 const int32_t slots=Length(array);
 if(slots%allocation_abi::kSlots)return Fail(error,"Transaction root slot count is inconsistent");
 const size_t count=size_t(slots/allocation_abi::kSlots);
 const auto owners=Read<hierarchy_allocation_abi::Vector>(holder.redefinitions);
 const uintptr_t begin=uintptr_t(owners.begin),end=uintptr_t(owners.end),capacity=uintptr_t(owners.capacity);
 constexpr size_t stride=sizeof(redefinition_abi::ClassRedefinition);
 if((!begin&&(end||capacity)) || (begin&&(begin%alignof(redefinition_abi::ClassRedefinition))) ||
    end<begin||capacity<end||(end-begin)%stride||(capacity-begin)%stride||(end-begin)/stride!=count)
  return Fail(error,"Transaction owner vector range is inconsistent");
 if(!BitsValid(holder.initialized,count)||!BitsValid(holder.actually_structural,count)||!BitsValid(holder.initial_structural,count))return Fail(error,"Transaction structural bitvector range is inconsistent");
 *output={raw,array,count,owners,holder.initialized,holder.actually_structural,holder.initial_structural};return true;
}
bool TransactionMetadataReader::ReadRoots(const void* raw,TransactionRootView* output,std::string* error)const{
 if(!root_||!Aligned(raw,alignof(allocation_abi::Iterator))||!output)return Fail(error,"Invalid transaction iterator context");
 const auto iterator=Read<allocation_abi::Iterator>(raw);TransactionHolderView holder;
 if(iterator.index<0||!ReadHolder(iterator.holder,&holder,error))return Fail(error,"Invalid transaction iterator holder");
 const size_t index=size_t(iterator.index);if(index>=holder.count)return Fail(error,"Transaction iterator is outside root slots");
 auto slot=[&](size_t n){return Element(holder.array,allocation_abi::kSlots*index+n);};
 *output={holder,index,const_cast<unsigned char*>(holder.owners.begin)+index*sizeof(redefinition_abi::ClassRedefinition),
     Bit(holder.initialized,index),Bit(holder.actually_structural,index),Bit(holder.initial_structural,index),
     slot(hierarchy_allocation_abi::kCacheSlot),slot(hierarchy_allocation_abi::kClassSlot),
     slot(allocation_abi::kOldInstancesSlot),slot(10),slot(allocation_abi::kOldClassesSlot),slot(allocation_abi::kNewClassesSlot)};
 // Allocation stages may legitimately not have materialized the later arrays.
 // Commit and allocation validation impose their own phase-specific presence.
 return true;
}
bool TransactionMetadataReader::ReadCache(void* raw,hierarchy_allocation_abi::Cache* output,std::string* error)const{
 if(!root_||!Aligned(raw,4)||!output)return Fail(error,"Invalid transaction DexCache context");
 const auto cache=Read<hierarchy_allocation_abi::Cache>(raw);
 auto pairs=[](uint64_t address,uint32_t count,size_t alignment){return count<=1024&&
     ((address&&!(address&(alignment-1)))||(!address&&!count));};
 if(!cache.prefix.dex_file || !pairs(cache.types,cache.num_types,8)||!pairs(cache.methods,cache.num_methods,16)||!pairs(cache.fields,cache.num_fields,16))return Fail(error,"Transaction DexCache array range is inconsistent");
 *output=cache;return true;
}
bool TransactionMetadataReader::ReadMethods(void* klass,MethodStorageView* output,std::string* error)const{
 if(!root_||!klass||!output)return Fail(error,"Missing method storage class");
 auto* array=reinterpret_cast<void*>(Read<uint64_t>(klass,offsetof(Class,methods)));
 if(array&&!Aligned(array,alignof(Method)))return Fail(error,"Method storage base is misaligned");
 uint32_t count=array?Read<uint32_t>(array):0;
 method_array::Boundaries boundaries;
 if(!method_array::ReadBoundaries(33,klass,sizeof(Class),count,method_array::Kind::Ordinary,nullptr,&boundaries))
  return Fail(error,"Method storage partitions are inconsistent");
 static_assert(sizeof(size_t)==8);
 const size_t bytes=array?art_method_abi::kArrayDataOffset+size_t(count)*sizeof(Method):0;
 method_array::View view;
 if(!method_array::Read(33,array,bytes,boundaries,&view))return Fail(error,"Method storage address range overflow");
 for(uint32_t i=0;i<count;++i){
  auto* method=view.At(i);
  auto* declaring=root_(method);if(!declaring)return Fail(error,"Method storage member lacks a declaring class");
  const uint32_t flags=Flags(method);const bool is_copied=(flags&(0x1000000|0x80000000))==0x1000000;
  if(declaring!=klass&&(!is_copied||!(Read<uint32_t>(declaring,offsetof(Class,access_flags))&0x200)))return Fail(error,"Method storage member has an invalid declaring class");
 }
 *output={array,count,static_cast<uint16_t>(boundaries.direct_count),static_cast<uint16_t>(boundaries.declared_count)};return true;
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
  const uint32_t status=__atomic_load_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(old)+offsetof(Class,status)),__ATOMIC_SEQ_CST)>>28;
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
