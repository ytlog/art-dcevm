#include "platform/common/native_initialized_transfer.h"
#include "platform/common/class_metadata_abi.h"
namespace artdcevm::native_initialized_transfer {
namespace {
bool Status(const modern_field_metadata::FieldMetadata& metadata,const void* klass,
    const modern_field_metadata::ReadContext* context,uint32_t* out)noexcept {
 const auto& layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?
     class_metadata::kLegacy:class_metadata::k17;
 size_t extent=0;class_metadata::State state{};
 if(!modern_field_metadata::ReadMappingExtent(context,klass,&extent)||
    !class_metadata::ReadState(layout,klass,extent,&state))return false;
 *out=state.status;return true;
}
bool Pair(const modern_field_metadata::FieldMetadata& metadata,const void* old,
    const void* next,const modern_field_metadata::ReadContext* context,uint32_t* destination)noexcept {
 uint32_t before=0,after=0;
 if(!old||!next||old==next||!Status(metadata,old,context,&before)||
    !Status(metadata,next,context,&after)||before<14||before>15||
    (after!=12&&after!=14&&after!=15))return false;
 // Fixed Class::CheckSetStatus permits only forward transitions; 14 -> 15
 // is the sole transition from initialized. Never regress 14/15 to active 13.
 *destination=before>after?before:after;return true;
}
}
bool Proof::Prepare(const source_commit::Plan& plan,
    const modern_field_metadata::FieldMetadata& metadata,
    const modern_field_storage::Service& fields,
    const modern_field_metadata::ReadContext* context){
 plan_=nullptr;metadata_=nullptr;fields_=nullptr;context_=nullptr;mapping_owner_=nullptr;
 if(!context||!context->owner||!context->extent||!metadata.Calibrated()||!fields.Calibrated()||
    metadata.StorageModel()!=fields.StorageModel()||plan.old_classes.empty()||
    plan.old_classes.size()!=plan.new_classes.size())return false;
 for(size_t i=0;i<plan.old_classes.size();++i){uint32_t destination=0;
  if(!Pair(metadata,plan.old_classes[i],plan.new_classes[i],context,&destination))return false;}
 plan_=&plan;metadata_=&metadata;fields_=&fields;context_=context;mapping_owner_=context->owner;return true;
}
bool Proof::Validate(const source_commit::Plan& plan,
    const modern_field_metadata::ReadContext* context)const noexcept{
 // A final read must use the actual owner's stopped mapping view. No OS fallback.
 if(context!=context_||!context||context->owner!=mapping_owner_||!context->extent||plan_!=&plan||!metadata_||!fields_||
    !metadata_->Calibrated()||!fields_->Calibrated()||plan.old_classes.empty()||
    plan.old_classes.size()!=plan.new_classes.size())return false;
 for(size_t i=0;i<plan.old_classes.size();++i){uint32_t destination=0;
  if(!Pair(*metadata_,plan.old_classes[i],plan.new_classes[i],context,&destination))return false;}
 return true;
}
uint32_t Proof::DestinationStatus(const source_commit::Plan& plan,size_t i,
    const modern_field_metadata::ReadContext* context)const noexcept{
 uint32_t destination=0;
 if(plan_!=&plan||!metadata_||context!=context_||!context||context->owner!=mapping_owner_||!context->extent||
    i>=plan.old_classes.size()||i>=plan.new_classes.size()||
    !Pair(*metadata_,plan.old_classes[i],plan.new_classes[i],context,&destination))return 0;
 return destination;
}
}
