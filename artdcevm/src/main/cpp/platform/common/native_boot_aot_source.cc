#include "platform/common/native_boot_aot_source.h"
#include <cstring>
namespace artdcevm::native_boot_aot_unaffected {
bool Source::Verify(void*method,uint32_t pc,const code_info_view::View&view,uint32_t row,
 const decoded_frame_recovery::RegisterRange&range,const modern_field_metadata::ReadContext&context)const noexcept{
 if(!self_||!stop_.IsStopped(self_)||!roots_.FinalRootWindow()||!metadata_.Calibrated()||!vtables_.Ready()||
    methods_.identities.size()<3||methods_.identities.size()!=methods_.readable_extents.size()||
    !plan_.old_classes.size()||plan_.old_classes.size()!=plan_.new_classes.size()||!TablesMapped(view,context)||!EmptyRootMasks(view,row)||view.InlineInfos().rows)return false;
 const auto*entry=catalog_.Lookup(method,roots_,context);
 if(!entry||entry->body!=native_boot_method_catalog::Body::kPrimitiveLocal||entry->max_locals<=0||
    (entry->modifiers&0x508u)!=0x8u||range.first||range.count!=uint32_t(entry->max_locals))return false;
 size_t extent=0;if(!context.extent(context.owner,method,&extent))return false;
 const auto&layout=methods_.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 method_metadata::Identity identity;
 if(!method_metadata::ReadIdentity(layout,method,extent,&identity)||identity.dex_method_index==UINT32_MAX||
    (identity.access_flags&0x40508u)!=0x8u)return false; // static, non-native, non-abstract, not obsolete
 void*declaring=metadata_.Reference(method,0);
 constexpr size_t loader=offsetof(class_metadata::Class11,class_loader);
 if(!declaring||!context.extent(context.owner,declaring,&extent)||extent<loader+4||metadata_.Reference(declaring,loader))return false;
 field_copy::CopyClassView named;
 if(!metadata_.ClassIdentity(declaring,&named,nullptr,&context)||!named.descriptor||entry->declaring_signature!=named.descriptor)return false;
 for(size_t i=0;i<plan_.old_classes.size();++i){
  if(declaring==plan_.old_classes[i]||declaring==plan_.new_classes[i]||
     !native_boot_aot_prefix::Observe(plan_,i,metadata_,vtables_,context).preserved)return false;
 }
 receiver_guard::CodeView source;
 if(!code_.Code(self_,method,&source,context)||!BodyMatches(*entry,source,identity.dex_method_index,pc,range,context))return false;
 return stop_.IsStopped(self_)&&roots_.FinalRootWindow()&&catalog_.Lookup(method,roots_,context)==entry;
}
}
