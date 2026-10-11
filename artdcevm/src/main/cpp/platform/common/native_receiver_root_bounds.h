#pragma once
#include "platform/common/native_receiver_original_ingress.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/native_receiver_dex_view.h"
#include "platform/abi/arm64/common/class_relationships.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include <array>
#include <algorithm>
#include <cstring>
namespace artdcevm::receiver_guard {
// Concrete original method/field ingress provider. Every inventory is rebuilt from
// that callback's live JNI root and ArtMethod; no map/cache authorizes pointers.
// Fixed common Class prefixes are independently calibrated by FieldMetadata.
// ART owns the reached Class/native arrays/DexFile until this no-safepoint
// shared-mutator borrow ends. Field listener Handle slots use the same inventory;
// Calibrated source-frame roots cover invoke listener and field VarHandle;
// invoke roots are built only after source resolution/frame reread;
// MethodHandle ingress still requires its independently owned source operands.
class OriginalRootBounds final:public OriginalIngress::TypedBounds {
 public:
 OriginalRootBounds(EntrySource&entry,const modern_field_metadata::FieldMetadata&metadata)
   :entry_(entry),metadata_(metadata){}
 // Ordinary setup only. A named typed leaf grants provenance to its fresh
 // ClassExt strong-root result; it grants no arbitrary address or Guard proof.
 bool BindHistory(HookEngine&hooks)noexcept {
  if(history_||inventory_.owner||!metadata_.Calibrated()||!entry_.OwnsMetadata(metadata_))return false;
  auto leaf=reinterpret_cast<abi::arm64::android_common::ObsoleteClass>(
      hooks.FindSymbol("libart.so",abi::arm64::android_common::kObsoleteClass));
  if(!leaf)return false;history_=leaf;return true;
 }
 bool Enter(const abi::common::RunnableJniScope&actual,void*method,jobject receiver,
            modern_field_metadata::ReadContext*out)noexcept override {
  return EnterMember(actual,method,false,receiver,out);
 }
 bool EnterField(const abi::common::RunnableJniScope&actual,void*field,jobject receiver,
                 modern_field_metadata::ReadContext*out)noexcept override {
  return EnterMember(actual,field,true,receiver,out);
 }
 bool EnterListenerField(void*self,void*caller,void*field,const uint32_t*slot,
                         modern_field_metadata::ReadContext*out)noexcept override {
  if(!out)return false;*out={};
  if(inventory_.owner||!metadata_.Calibrated()||!entry_.OwnsMetadata(metadata_))return false;
  EntrySource::OriginalRoots roots;
  if(!entry_.SeedListenerFieldRoots(self,caller,field,slot,&roots))return false;
  auto&i=inventory_;i.owner=this;i.actual=nullptr;i.listener_thread=self;i.count=0;i.class_count=0;
  return Build(self,field,true,roots,out,caller);
 }
 bool EnterSourceFrame(void*self,void*caller,void*member,bool field,void*receiver,void*special,
                          modern_field_metadata::ReadContext*out)noexcept override {
  if(!out)return false;*out={};
  if(inventory_.owner||!metadata_.Calibrated()||!entry_.OwnsMetadata(metadata_))return false;
  EntrySource::OriginalRoots roots;
  if(!entry_.SeedSourceFrameRoots(self,caller,member,field,receiver,special,&roots))return false;
  auto&i=inventory_;i.owner=this;i.actual=nullptr;i.listener_thread=self;i.count=0;i.class_count=0;
  return Build(self,member,field,roots,out,caller);
 }
 private:
 bool EnterMember(const abi::common::RunnableJniScope&actual,void*method,bool field,jobject receiver,
                  modern_field_metadata::ReadContext*out)noexcept {
  if(!out)return false;*out={};
  if(inventory_.owner||!metadata_.Calibrated()||!entry_.OwnsMetadata(metadata_))return false;
  EntrySource::OriginalRoots roots;
  if(!(field?entry_.SeedOriginalFieldRoots(actual,method,receiver,&roots):entry_.SeedOriginalRoots(actual,method,receiver,&roots)))return false;
  auto&i=inventory_;i.owner=this;i.actual=&actual;i.listener_thread=nullptr;i.count=0;i.class_count=0;
  return Build(actual.self,method,field,roots,out);
 }
 bool Build(void*self,void*method,bool field,const EntrySource::OriginalRoots&roots,
            modern_field_metadata::ReadContext*out,void*caller=nullptr)noexcept {
  auto&i=inventory_;
  bool ok=Add(self,4)&&Add(method,field?sizeof(field_array::Field):8)&&Class(roots.declaring);
  if(ok&&caller)ok=Add(caller,8)&&Class(roots.caller_declaring);
  if(ok&&roots.local_slot)ok=Add(roots.local_slot,4);
  if(ok&&roots.receiver)ok=Add(roots.receiver,8)&&Class(metadata_.Reference(roots.receiver,0));
  for(size_t n=0;ok&&n<i.class_count;++n)ok=ExpandClass(i.classes[n]);
  if(ok&&history_)ok=ValidateHistory();
  if(ok&&field)ok=OwnsField(roots.declaring,method);
  if(!ok||!Current()){Reset();return false;}
  *out={this,Extent};return true;
 }
 public:
 void Leave()noexcept override {if(inventory_.owner!=this)std::abort();Reset();}
 private:
 struct Range {uintptr_t begin;size_t size;};
 // A bounded native stack-like TLS inventory: no allocation and no managed
 // pointer survives Leave. Deep/custom hierarchies refuse instead of widening.
 struct Inventory {
  OriginalRootBounds*owner;const abi::common::RunnableJniScope*actual;void*listener_thread;
  std::array<Range,1024>ranges;std::array<void*,128>classes,history;
  size_t count,class_count;
 };
 inline static thread_local Inventory inventory_{};
 bool Current()const noexcept{return inventory_.owner==this&&(inventory_.actual?entry_.OriginalScopeCurrent(*inventory_.actual):entry_.ListenerThreadCurrent(inventory_.listener_thread));}
 void Reset()noexcept {auto&i=inventory_;i.ranges.fill({0,0});i.classes.fill(nullptr);i.history.fill(nullptr);i.owner=nullptr;i.actual=nullptr;i.listener_thread=nullptr;i.count=0;i.class_count=0;}
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
 bool OwnsField(void*klass,void*field)noexcept {
  const auto&layout=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?
      field_array::kSplit:field_array::kMerged17;
  // Exact physical membership in that live declaring Class-owned native array
  // closes the decoded field provenance; a matching header alone is insufficient.
  for(bool statics:{false,true}){
   void*array=nullptr;if(!field_array::ReadPointer(layout,klass,layout.class_size,statics,&array))return false;
   if(!array)continue;size_t extent=0;field_array::View view;
   if(!Extent(this,array,&extent)||!field_array::Read(layout,array,extent,statics,&view))return false;
   uint32_t index=0;if(view.IndexOf(field,&index))return true;
  }
  return false;
 }
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
  if(!NativeFields(klass,fields,false)||(!fields.merged&&!NativeFields(klass,fields,true))||
      !Interfaces(klass)||!Dex(klass)||!Current())return false;
  if(history_){
   // GetObsoleteClass -> ClassExt::obsolete_class_ uses ART's original read
   // barrier and returns ObjPtr<Class> under this shared-mutator borrow. No
   // ClassExt field offset, JNI decoder, map or cached pointer is consulted.
   auto*old=reinterpret_cast<void*>(history_(klass).reference);
   if(!Current()||!Class(old))return false;
   for(size_t n=0;n<inventory_.class_count;++n)if(inventory_.classes[n]==klass){inventory_.history[n]=old;break;}
  }
  return Current();
 }
 bool HistoryIdentity(void*klass,field_copy::CopyClassView*out)noexcept {
  const auto&layout=metadata_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
  class_metadata::State state;class_metadata::Identity identity;
  if(!Current()||!class_metadata::ReadState(layout,klass,layout.size,&state)||state.IsPrimitive()||
     (state.access_flags&0x40000u)||metadata_.Reference(klass,offsetof(class_metadata::Class11,component_type))||
     !class_metadata::ReadIdentity(layout,klass,layout.size,&identity)||identity.class_def_index<0||identity.dex_type_index<0)return false;
  auto*meta=metadata_.Reference(klass,0);
  if(!meta||metadata_.Reference(meta,0)!=meta)return false;
  auto*cache=metadata_.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
  if(!cache)return false;
  auto*file=reinterpret_cast<void*>(uintptr_t(Scalar<uint64_t>(cache,offsetof(abi::arm64::android11::LegacyCache,dex_file))));
  if(!file)return false;dex_mapping_abi::Prefix prefix;std::memcpy(&prefix,file,sizeof(prefix));
  modern_field_metadata::ReadContext context{this,Extent};
  BoundedDexView dex({prefix.begin,Scalar<uint32_t>(prefix.begin,32),prefix.data_begin,prefix.data_size,uint32_t(identity.class_def_index)});
  const char*descriptor=nullptr;uint32_t flags=0,type=0;
  if(!dex.Initialize()||!dex.Identity(&descriptor,&flags,&type)||type!=uint32_t(identity.dex_type_index)||
     (flags&0x7631u)!=(state.access_flags&0x7631u)||!metadata_.ClassIdentity(klass,out,nullptr,&context)||
     !out->descriptor||std::strcmp(out->descriptor,descriptor)||!Current())return false;
  // The inventory provenance is the original typed root, never this descriptor.
  out->descriptor=descriptor;return true;
 }
 bool ValidateHistory()noexcept {
  const auto&i=inventory_;
  for(size_t n=0;n<i.class_count;++n){
   if(!i.history[n])continue;
   field_copy::CopyClassView owner;if(!HistoryIdentity(i.classes[n],&owner))return false;
   void*old=i.history[n];size_t steps=0;
   while(old){
    if(++steps>=i.class_count||old==i.classes[n])return false;
    size_t at=0;for(;at<i.class_count&&i.classes[at]!=old;++at){}
    if(at==i.class_count)return false;
    field_copy::CopyClassView identity;
    if(!HistoryIdentity(old,&identity)||identity.loader!=owner.loader||std::strcmp(identity.descriptor,owner.descriptor))return false;
    old=i.history[at];
   }
  }
  return Current();
 }
 static bool Extent(void*p,const void*address,size_t*out)noexcept {
  if(!out)return false;*out=0;auto&owner=*static_cast<OriginalRootBounds*>(p);
  if(!address||!owner.Current())return false;
  const auto start=uintptr_t(address);const auto&i=inventory_;
  for(size_t n=0;n<i.count;++n){const auto&r=i.ranges[n];if(start>=r.begin&&start-r.begin<r.size)*out=std::max(*out,r.size-size_t(start-r.begin));}
  return *out!=0&&owner.Current();
 }
 EntrySource&entry_;const modern_field_metadata::FieldMetadata&metadata_;
 abi::arm64::android_common::ObsoleteClass history_=nullptr;
};
}
