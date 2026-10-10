#pragma once
#include "platform/common/native_abstract_instances.h"
#include <algorithm>
#include <cstring>
namespace artdcevm::native_boot_aot_prefix {
// Necessary source layout observation only. This does not authorize compiled
// code: the actual PC/code dependency and active indirect operands remain the
// frame owner's separate proof. Every read uses the caller's live stop context.
struct Report {bool preserved=false;size_t boot_classes=0,fields=0,slots=0;const char*failure="not-observed";};
inline Report Observe(const source_commit::Plan&plan,size_t pair,
 const modern_field_metadata::FieldMetadata&metadata,const native_abstract_instances::Service&vtables,
 const modern_field_metadata::ReadContext&context){
 Report r;auto deny=[&](const char*s){r.failure=s;return r;};
 if(!metadata.Calibrated()||!vtables.Ready()||!context.owner||!context.extent||plan.old_classes.size()!=plan.new_classes.size()||pair>=plan.old_classes.size())return deny("boot-prefix-lacks-exact-paired-plan-and-calibrated-owners");
 constexpr size_t loader=offsetof(class_metadata::Class11,class_loader),parent=offsetof(class_metadata::Class11,super_class);
 static_assert(loader==offsetof(class_metadata::Class17,class_loader)&&parent==offsetof(class_metadata::Class17,super_class));
 const size_t object_size=metadata.StorageModel()==modern_field_metadata::Model::kSplit?offsetof(class_metadata::Class11,object_size):offsetof(class_metadata::Class17,object_size);
 static_assert(offsetof(class_metadata::Class16,object_size)==offsetof(class_metadata::Class17,object_size));
 auto mapped=[&](const void*p,size_t needed){size_t extent=0;return p&&context.extent(context.owner,p,&extent)&&extent>=needed;};
 auto changed=[&](void*k){return std::find(plan.old_classes.begin(),plan.old_classes.end(),k)!=plan.old_classes.end()||std::find(plan.new_classes.begin(),plan.new_classes.end(),k)!=plan.new_classes.end();};
 auto ordinary=[&](void*k){return mapped(k,object_size+4);};
 for(size_t i=0;i<plan.old_classes.size();++i)if(!ordinary(plan.old_classes[i])||!ordinary(plan.new_classes[i])||!metadata.Reference(plan.old_classes[i],loader)||!metadata.Reference(plan.new_classes[i],loader))return deny("boot-prefix-changed-Class-is-boot-or-unmapped");
 void*old=plan.old_classes[pair],*next=plan.new_classes[pair];
 void*ancestor=metadata.Reference(old,parent);if(!ancestor||metadata.Reference(next,parent)!=ancestor)return deny("boot-prefix-original-superclass-changed");
 uint32_t old_size=0,new_size=0;std::memcpy(&old_size,static_cast<char*>(old)+object_size,4);std::memcpy(&new_size,static_cast<char*>(next)+object_size,4);
 native_abstract_instances::VTableView left,right;
 if(!vtables.ReadVTable(old,metadata,&left,nullptr,&context)||!vtables.ReadVTable(next,metadata,&right,nullptr,&context))return deny("boot-prefix-replacement-vtable-unproved");
 for(size_t depth=0;ancestor;++depth){
  if(depth==1024||!ordinary(ancestor)||changed(ancestor))return deny("boot-prefix-ancestor-changed-unmapped-or-cyclic");
  if(!metadata.Reference(ancestor,loader)){
   ++r.boot_classes;uint32_t inherited_size=0;std::memcpy(&inherited_size,static_cast<char*>(ancestor)+object_size,4);
   if(inherited_size<sizeof(abi::arm64::ObjectPrefix)||old_size<inherited_size||new_size<inherited_size)return deny("boot-prefix-inherited-object-storage-truncated");
   field_array::View fields;if(!metadata.Fields(ancestor,false,&fields,nullptr,&context))return deny("boot-prefix-source-field-table-unproved");
   for(uint32_t i=0;i<fields.selected_count;++i){auto*field=static_cast<abi::arm64::Field*>(fields.AtSelected(i));if(!mapped(field,sizeof(*field))||metadata.FieldDeclaringClass(field)!=ancestor)return deny("boot-prefix-field-declaring-owner-unproved");
    uint32_t offset=0;std::memcpy(&offset,&field->offset,sizeof(offset));
    // The original source ArtField is unchanged and inherited from the SAME
    // original ancestor; its offset is not recomputed from candidate fields.
    field_copy::CopyFieldView identity;if(!metadata.FieldIdentity(field,&identity,nullptr,&context)||identity.field!=field||!identity.descriptor)return deny("boot-prefix-field-type-unproved");
    const char type=identity.descriptor[0];const size_t width=type=='J'||type=='D'?8:type=='S'||type=='C'?2:type=='B'||type=='Z'?1:type=='I'||type=='F'||type=='L'||type=='['?4:0;
    if(!width||offset<sizeof(abi::arm64::ObjectPrefix)||offset>inherited_size||width>inherited_size-offset)return deny("boot-prefix-field-offset-outside-original-inherited-storage");++r.fields;
   }
   native_abstract_instances::VTableView boot;if(!vtables.ReadVTable(ancestor,metadata,&boot,nullptr,&context)||left.count<boot.count||right.count<boot.count)return deny("boot-prefix-original-vtable-length-unproved");
   for(size_t i=0;i<boot.count;++i){uint64_t a=0,b=0;std::memcpy(&a,static_cast<char*>(left.table)+left.payload+i*8,8);std::memcpy(&b,static_cast<char*>(right.table)+right.payload+i*8,8);if(a!=b)return deny("boot-prefix-active-dispatch-entry-changed");++r.slots;}
  }
  ancestor=metadata.Reference(ancestor,parent);
 }
 if(!r.boot_classes)return deny("boot-prefix-no-original-boot-ancestor");
 r.preserved=true;r.failure="";return r;
}
}
