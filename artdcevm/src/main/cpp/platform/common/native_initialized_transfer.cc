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
    const void* next,const modern_field_metadata::ReadContext* context,Proof::Decision* decision)noexcept {
 uint32_t before=0,after=0;
 if(!old||!next||old==next||!Status(metadata,old,context,&before)||!Status(metadata,next,context,&after))return false;
 // Fixed ClassStatus: 11 Verified, 12 SuperclassValidated. The actual source
 // allocator has already linked/verified the target. Preserve that source
 // result, never initialize it or repair a backwards allocator state here.
 if((before==11||before==12)&&(after==11||after==12)&&after>=before){
  *decision={Proof::Action::PreservePreInitialization,after};return true;
 }
 if((before!=14&&before!=15)||(after!=12&&after!=14&&after!=15))return false;
 // Completed initialization permits only the fixed forward completion family.
 // Active13 and erroneous2/3 need distinct episode/exception authority.
 *decision={Proof::Action::CompleteInitialization,before>after?before:after};return true;
}
}
bool Proof::Prepare(const source_commit::Plan& plan,
    const modern_field_metadata::FieldMetadata& metadata,
    const modern_field_storage::Service& fields,
    const modern_field_metadata::ReadContext* context){
 plan_=nullptr;metadata_=nullptr;fields_=nullptr;context_=nullptr;mapping_owner_=nullptr;
 actions_.clear();old_slots_=new_slots_=nullptr;
 if(!context||!context->owner||!context->extent||!metadata.Calibrated()||!fields.Calibrated()||
    metadata.StorageModel()!=fields.StorageModel()||plan.old_classes.empty()||
    plan.old_classes.size()!=plan.new_classes.size())return false;
 actions_.reserve(plan.old_classes.size());
 for(size_t i=0;i<plan.old_classes.size();++i){Decision decision;
  if(!Pair(metadata,plan.old_classes[i],plan.new_classes[i],context,&decision))return false;
  uint32_t before=0,after=0;
  if(!Status(metadata,plan.old_classes[i],context,&before)||!Status(metadata,plan.new_classes[i],context,&after))return false;
  actions_.push_back({decision.action,before,after});}
 old_slots_=plan.old_classes.data();new_slots_=plan.new_classes.data();
 plan_=&plan;metadata_=&metadata;fields_=&fields;context_=context;mapping_owner_=context->owner;return true;
}
bool Proof::Validate(const source_commit::Plan& plan,
    const modern_field_metadata::ReadContext* context)const noexcept{
 // A final read must use the actual owner's stopped mapping view. No OS fallback.
 if(context!=context_||!context||context->owner!=mapping_owner_||!context->extent||plan_!=&plan||!metadata_||!fields_||
    !metadata_->Calibrated()||!fields_->Calibrated()||plan.old_classes.empty()||
    plan.old_classes.size()!=plan.new_classes.size()||actions_.size()!=plan.old_classes.size()||
    old_slots_!=plan.old_classes.data()||new_slots_!=plan.new_classes.data())return false;
 for(size_t i=0;i<plan.old_classes.size();++i){Decision decision;
  if(!Pair(*metadata_,plan.old_classes[i],plan.new_classes[i],context,&decision)||decision.action!=actions_[i].action)return false;
  uint32_t before=0,after=0;
  if(!Status(*metadata_,plan.old_classes[i],context,&before)||!Status(*metadata_,plan.new_classes[i],context,&after)||
     before<actions_[i].old_status||after<actions_[i].new_status)return false;}
 return true;
}
Proof::Decision Proof::Transfer(const source_commit::Plan& plan,size_t i,
    const modern_field_metadata::ReadContext* context)const noexcept{
 Decision decision;
 if(plan_!=&plan||!metadata_||context!=context_||!context||context->owner!=mapping_owner_||!context->extent||
    old_slots_!=plan.old_classes.data()||new_slots_!=plan.new_classes.data()||
    i>=actions_.size()||i>=plan.old_classes.size()||i>=plan.new_classes.size()||
    !Pair(*metadata_,plan.old_classes[i],plan.new_classes[i],context,&decision)||decision.action!=actions_[i].action)return {};
 uint32_t before=0,after=0;
 if(!Status(*metadata_,plan.old_classes[i],context,&before)||!Status(*metadata_,plan.new_classes[i],context,&after)||
    before<actions_[i].old_status||after<actions_[i].new_status)return {};
 return decision;
}
uint32_t Proof::DestinationStatus(const source_commit::Plan& plan,size_t i,
    const modern_field_metadata::ReadContext* context)const noexcept{return Transfer(plan,i,context).status;}
}
