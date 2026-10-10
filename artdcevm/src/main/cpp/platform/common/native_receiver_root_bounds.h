#pragma once
#include "platform/common/native_receiver_original_ingress.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include <array>
#include <algorithm>
#include <cstring>
namespace artdcevm::receiver_guard {
// Concrete original-method ingress provider. Every inventory is rebuilt from
// that callback's live JNI root and ArtMethod; no map/cache authorizes pointers.
// Fixed common Class prefixes are independently calibrated by FieldMetadata.
// ART owns the reached Class/native arrays/DexFile until this no-safepoint
// shared-mutator borrow ends. It does not support field/handle/listener ingress.
class OriginalRootBounds final:public OriginalIngress::TypedBounds {
 public:
 OriginalRootBounds(EntrySource&entry,const modern_field_metadata::FieldMetadata&metadata)
   :entry_(entry),metadata_(metadata){}
 bool Enter(const abi::common::RunnableJniScope&actual,void*method,jobject receiver,
            modern_field_metadata::ReadContext*out)noexcept override {
  if(!out)return false;*out={};
  if(inventory_.owner||!metadata_.Calibrated()||!entry_.OwnsMetadata(metadata_))return false;
  EntrySource::OriginalRoots roots;
  if(!entry_.SeedOriginalRoots(actual,method,receiver,&roots))return false;
  auto&i=inventory_;i.owner=this;i.actual=&actual;i.count=0;i.class_count=0;
  bool ok=Add(actual.self,4)&&Add(method,8)&&Class(roots.declaring);
  if(ok&&roots.local_slot)ok=Add(roots.local_slot,4);
  if(ok&&roots.receiver)ok=Add(roots.receiver,8)&&Class(metadata_.Reference(roots.receiver,0));
  for(size_t n=0;ok&&n<i.class_count;++n)ok=ExpandClass(i.classes[n]);
  if(!ok||!Current()){Reset();return false;}
  *out={this,Extent};return true;
 }
 void Leave()noexcept override {if(inventory_.owner!=this)std::abort();Reset();}
 private:
 struct Range {uintptr_t begin;size_t size;};
 // A bounded native stack-like TLS inventory: no allocation and no managed
 // pointer survives Leave. Deep/custom hierarchies refuse instead of widening.
 struct Inventory {
  OriginalRootBounds*owner;const abi::common::RunnableJniScope*actual;
  std::array<Range,1024>ranges;std::array<void*,128>classes;
  size_t count,class_count;
 };
 inline static thread_local Inventory inventory_{};
 bool Current()const noexcept{return inventory_.owner==this&&inventory_.actual&&entry_.OriginalScopeCurrent(*inventory_.actual);}
 void Reset()noexcept {auto&i=inventory_;i.ranges.fill({0,0});i.classes.fill(nullptr);i.owner=nullptr;i.actual=nullptr;i.count=0;i.class_count=0;}
 bool Add(const void*p,size_t size)noexcept {
  auto&i=inventory_;auto begin=uintptr_t(p);
  if(!p||!size||begin>UINTPTR_MAX-size)return false;
  for(size_t n=0;n<i.count;++n)if(i.ranges[n].begin==begin){if(i.ranges[n].size<size)i.ranges[n].size=size;return true;}
  if(i.count==i.ranges.size())return false;i.ranges[i.count++]={begin,size};return true;
 }
 bool Class(void*klass)noexcept {
  if(!klass)return true;
  auto&i=inventory_;if(uintptr_t(klass)%4)return false;
  for(size_t n=0;n<i.class_count;++n)if(i.classes[n]==klass)return true;
  if(i.class_count==i.classes.size())return false;
  const size_t prefix=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?
      class_metadata::kLegacy.size:class_metadata::k17.size;
  if(!Add(klass,prefix))return false;i.classes[i.class_count++]=klass;return true;
 }
 template<class T>static T Scalar(const void*p,size_t offset)noexcept {T value;std::memcpy(&value,static_cast<const char*>(p)+offset,sizeof(value));return value;}
 bool NativeFields(void*klass,const field_array::Layout&layout,bool statics)noexcept {
  void*array=nullptr;
  if(!field_array::ReadPointer(layout,klass,layout.class_size,statics,&array))return false;
  if(!array)return true;
  // LengthPrefixedArray<ArtField> is the original Class-owned native array,
  // not an arbitrary pointer whose guessed header grants a mapped extent.
  if(uintptr_t(array)%4||!Add(array,4))return false;
  auto count=Scalar<uint32_t>(array,0);if(count>65536)return false;
  return Add(array,4+size_t(count)*sizeof(field_array::Field));
 }
 bool Dex(void*klass)noexcept {
  void*cache=metadata_.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
  if(!cache)return true;
  constexpr size_t slot=offsetof(abi::arm64::android11::LegacyCache,dex_file);
  // Both fixed DexCache families share the reference prefix and DexFile slot.
  if(!Add(cache,slot+8)||!Class(metadata_.Reference(cache,0)))return false;
  auto*file=reinterpret_cast<void*>(uintptr_t(Scalar<uint64_t>(cache,slot)));
  if(!file||uintptr_t(file)%8||!Add(file,sizeof(dex_mapping_abi::Prefix)))return false;
  dex_mapping_abi::Prefix dex;std::memcpy(&dex,file,sizeof(dex));
  const auto begin=uintptr_t(dex.begin),data=uintptr_t(dex.data_begin);
  // API35+ unused_size_ is deliberately ignored. Only the live source-owned
  // data range grants bytes. Separate CompactDex header storage is unsupported.
  if(!begin||!data||dex.data_size>UINTPTR_MAX-data||begin<data||
     begin-data>dex.data_size||dex.data_size-(begin-data)<112)return false;
  if(!Add(dex.data_begin,dex.data_size)||std::memcmp(dex.begin,"dex\n",4)||dex.begin[7])return false;
  const auto file_size=Scalar<uint32_t>(dex.begin,32);
  if(file_size<112||file_size>dex.data_size-(begin-data))return false;
  return Add(dex.begin,file_size);
 }
 bool Interfaces(void*klass)noexcept {
  void*table=metadata_.Reference(klass,offsetof(class_metadata::Class11,iftable));
  if(!table)return true;
  // Class::iftable_ is the original typed IfTable/ObjectArray root. Its length
  // defines its ART allocation; never read a length from an unknown address.
  if(!Add(table,12)||!Class(metadata_.Reference(table,0)))return false;
  auto count=Scalar<int32_t>(table,8);if(count<0||(count&1)||count>65536)return false;
  if(!Add(table,12+size_t(count)*4))return false;
  for(int32_t n=0;n<count;n+=2)if(!Class(metadata_.Reference(table,12+size_t(n)*4)))return false;
  return true;
 }
 bool ExpandClass(void*klass)noexcept {
  if(!Current())return false;
  const auto&layout=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?
    class_metadata::kLegacy:class_metadata::k17;
  class_metadata::State state;
  if(!class_metadata::ReadState(layout,klass,layout.size,&state)||(state.access_flags&0x40000u))return false;
  if(!Class(metadata_.Reference(klass,0))||
     !Class(metadata_.Reference(klass,offsetof(class_metadata::Class11,super_class)))||
     !Class(metadata_.Reference(klass,offsetof(class_metadata::Class11,component_type))))return false;
  auto*loader=metadata_.Reference(klass,offsetof(class_metadata::Class11,class_loader));
  if(loader&&!Add(loader,8))return false;
  const auto&fields=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?
      field_array::kSplit:field_array::kMerged17;
  return NativeFields(klass,fields,false)&&(!fields.merged?NativeFields(klass,fields,true):true)&&
      Interfaces(klass)&&Dex(klass)&&Current();
 }
 static bool Extent(void*p,const void*address,size_t*out)noexcept {
  if(!out)return false;*out=0;auto&owner=*static_cast<OriginalRootBounds*>(p);
  if(!address||!owner.Current())return false;
  const auto start=uintptr_t(address);const auto&i=inventory_;
  for(size_t n=0;n<i.count;++n){const auto&r=i.ranges[n];if(start>=r.begin&&start-r.begin<r.size)*out=std::max(*out,r.size-size_t(start-r.begin));}
  return *out!=0&&owner.Current();
 }
 EntrySource&entry_;const modern_field_metadata::FieldMetadata&metadata_;
};
}
