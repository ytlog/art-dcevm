#pragma once
#include "platform/abi/arm64/art_field.h"
#include "platform/abi/arm64/common/class_relationships.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/prepared_root_bindings.h"
#include "platform/common/source_commit.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstring>
#include <cstdlib>
namespace artdcevm::native_static_field_aliases {
using ObsoleteClass=abi::arm64::android_common::ObsoleteClass;
inline constexpr auto kObsoleteClass=abi::arm64::android_common::kObsoleteClass;
// ART keeps the real native field allocation in its retained obsolete Class.
// Follow that named history rather than keeping a second process pointer map.
class Plan final {
 public:
 struct Entry {void* field;void* current;void* target;void* history_class;void* current_class;void* target_class;
  uint32_t flags,index,offset,target_flags=0,target_index=0,target_offset=0;};
 bool Prepare(HookEngine& hooks,const modern_field_metadata::FieldMetadata& metadata,
              const source_commit::Plan& source,std::unordered_map<void*,void*>* mappings,
              std::string* error,const modern_field_metadata::ReadContext* context){
  auto fail=[&](const char* e){if(error)*error=e;return false;};
  if(!mappings||!metadata.Calibrated()||source.old_classes.empty()||source.old_classes.size()!=source.new_classes.size())return fail("Static alias preparation lacks complete native Class pairs");
  auto history=reinterpret_cast<ObsoleteClass>(hooks.FindSymbol("libart.so",kObsoleteClass));
  if(!history)return fail("Missing named original static-field Class history source");
  Plan candidate;candidate.metadata_=&metadata;candidate.context_=context;candidate.history_=history;
  auto add=[&](Entry entry){
   if(entry.target){size_t extent=0;field_copy::CopyFieldView before,after;
    if(!modern_field_metadata::ReadMappingExtent(context,entry.target,&extent)||extent<sizeof(abi::arm64::Field)||metadata.FieldDeclaringClass(entry.target)!=entry.target_class||
       !metadata.FieldIdentity(entry.current,&before,error,context)||!metadata.FieldIdentity(entry.target,&after,error,context)||!before.name||!after.name||!before.descriptor||!after.descriptor||
       std::strcmp(before.name,after.name)||std::strcmp(before.descriptor,after.descriptor))return false;
    auto* target=static_cast<const abi::arm64::Field*>(entry.target);if(!(target->access_flags&8u))return false;
    entry.target_flags=target->access_flags;entry.target_index=target->dex_field_index;entry.target_offset=target->offset;
   }candidate.entries_.push_back(entry);return true;
  };
  for(size_t pair=0;pair<source.old_classes.size();++pair){
   void* current=source.old_classes[pair];void* next=source.new_classes[pair];
   field_copy::CopyClassView original;if(!metadata.ClassIdentity(current,&original,error,context)||!original.descriptor)return false;
   field_array::View fields;if(!metadata.Fields(current,true,&fields,error,context))return false;
   for(size_t i=0;i<fields.selected_count;++i){auto* f=static_cast<abi::arm64::Field*>(fields.AtSelected(i));auto it=mappings->find(f);
    if(!f||metadata.FieldDeclaringClass(f)!=current||!(f->access_flags&8u)||it==mappings->end())return fail("Current static alias source is not in exact prepared mapping");
    if(!add({f,f,it->second,current,current,next,f->access_flags,f->dex_field_index,f->offset}))return fail("Static alias replacement is not the exact current member identity");
   }
   std::unordered_set<void*> seen{current};void* old=current;
   for(;;){void* value=reinterpret_cast<void*>(history(old).reference);candidate.chain_.push_back({old,value});if(!value)break;
    if(!seen.insert(value).second)return fail("Cyclic original static-field Class history");
    field_copy::CopyClassView identity;if(!metadata.ClassIdentity(value,&identity,error,context)||!identity.descriptor||identity.loader!=original.loader||std::strcmp(identity.descriptor,original.descriptor))return fail("Historical static field belongs to foreign Class namespace");
    field_array::View historical;if(!metadata.Fields(value,true,&historical,error,context))return false;
    for(size_t i=0;i<historical.selected_count;++i){auto* f=static_cast<abi::arm64::Field*>(historical.AtSelected(i));
     if(!f||!(f->access_flags&8u))return fail("Historical static field allocation has wrong storage kind");
     // Already removed fields still declare their retained old owner. They
     // cannot be resurrected merely because a later definition reuses a name.
     if(metadata.FieldDeclaringClass(f)!=current)continue;
     field_copy::CopyFieldView name;if(!metadata.FieldIdentity(f,&name,error,context)||!name.name||!name.descriptor)return false;
     void* match=nullptr;
     for(size_t j=0;j<fields.selected_count;++j){auto* live=static_cast<abi::arm64::Field*>(fields.AtSelected(j));
      if(f->access_flags!=live->access_flags||f->dex_field_index!=live->dex_field_index||f->offset!=live->offset)continue;
      field_copy::CopyFieldView live_name;if(!metadata.FieldIdentity(live,&live_name,error,context))return false;
      if(!std::strcmp(name.name,live_name.name)&&!std::strcmp(name.descriptor,live_name.descriptor)){if(match)return fail("Ambiguous historical static field identity");match=live;}
     }
     if(!match)return fail("Historical static alias disagrees with current full native declaration");
     auto it=mappings->find(match);if(it==mappings->end())return fail("Historical static alias has no prepared current mapping");
     if(!mappings->emplace(f,it->second).second)return fail("Historical static alias duplicates a mapped native allocation");
     if(!add({f,match,it->second,value,current,next,f->access_flags,f->dex_field_index,f->offset}))return fail("Historical static replacement member identity differs");
    }old=value;
   }
  }
  if(!candidate.Validate())return fail("Static alias history changed during preparation");
  *this=std::move(candidate);return true;
 }
 bool CollectManagedRoots(const prepared_roots::Registrar& r){
  for(auto& e:entries_)if(!roots_.Add(&e.history_class,r)||!roots_.Add(&e.current_class,r)||!roots_.Add(&e.target_class,r))return false;
  for(auto& e:chain_)if(!roots_.Add(&e.owner,r)||!roots_.Add(&e.previous,r))return false;roots_.Seal();return true;
 }
 bool CanRefresh(const prepared_roots::Resolver& r)const noexcept{return roots_.CanRefresh(r);}
 void Refresh(const prepared_roots::Resolver& r)noexcept{roots_.Refresh(r);}
 bool Validate()const noexcept{
  if(!metadata_||!history_)return false;
  const size_t class_bytes=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy.size:class_metadata::k17.size;
  for(const auto& c:chain_){size_t extent=0;if(!modern_field_metadata::ReadMappingExtent(context_,c.owner,&extent)||extent<class_bytes||reinterpret_cast<void*>(history_(c.owner).reference)!=c.previous)return false;}
  for(const auto& e:entries_){size_t extent=0;
   if(!modern_field_metadata::ReadMappingExtent(context_,e.field,&extent)||extent<sizeof(abi::arm64::Field))return false;
   auto* f=static_cast<const abi::arm64::Field*>(e.field);
   if(metadata_->FieldDeclaringClass(e.field)!=e.current_class||f->access_flags!=e.flags||f->dex_field_index!=e.index||f->offset!=e.offset)return false;
   if(e.target){if(!modern_field_metadata::ReadMappingExtent(context_,e.target,&extent)||extent<sizeof(abi::arm64::Field))return false;
    const auto* target=static_cast<const abi::arm64::Field*>(e.target);if(metadata_->FieldDeclaringClass(e.target)!=e.target_class||target->access_flags!=e.target_flags||target->dex_field_index!=e.target_index||target->offset!=e.target_offset)return false;
   }
  }return true;
 }
 const std::vector<Entry>& Entries()const noexcept{return entries_;}
 void Rebind()const noexcept{
  for(const auto& e:entries_){auto* old=static_cast<abi::arm64::Field*>(e.field);const auto* current=static_cast<const abi::arm64::Field*>(e.target?e.target:e.current);
   // Fixed ArtField setters: scalar metadata and GcRoot<Class>::Assign.
   // This is a native root slot, not a heap reference requiring a card mark.
   // The caller owns the original exclusive mutator / disabled-GC commit.
   old->access_flags=current->access_flags;old->dex_field_index=current->dex_field_index;old->offset=current->offset;
   void* owner=e.target?e.target_class:e.current_class;
   if(!owner||reinterpret_cast<uintptr_t>(owner)>UINT32_MAX)std::abort();
   old->declaring_class=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(owner));
   if(metadata_->FieldDeclaringClass(old)!=owner||old->access_flags!=current->access_flags||old->dex_field_index!=current->dex_field_index||old->offset!=current->offset)std::abort();
  }
 }
 private:
 struct Chain {void* owner;void* previous;};
 const modern_field_metadata::FieldMetadata* metadata_=nullptr;
 const modern_field_metadata::ReadContext* context_=nullptr;
 ObsoleteClass history_=nullptr;
 std::vector<Entry> entries_;std::vector<Chain> chain_;prepared_roots::Bindings roots_;
};
}
