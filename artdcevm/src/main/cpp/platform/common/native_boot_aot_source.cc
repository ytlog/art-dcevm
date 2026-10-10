#include "platform/common/native_boot_aot_source.h"
#include <cstring>
namespace artdcevm::native_boot_aot_unaffected {
bool Source::Verify(void*method,uint32_t pc,const code_info_view::View&view,uint32_t row,
 const decoded_frame_recovery::RegisterRange&range,const modern_field_metadata::ReadContext&context,const char**failure)const noexcept{
 auto deny=[&](const char*reason){if(failure)*failure=reason;return false;};if(failure)*failure=nullptr;
 if(!self_||!stop_.IsStopped(self_)||!roots_.FinalRootWindow()||!metadata_.Calibrated()||!vtables_.Ready()||
    methods_.identities.size()<3||methods_.identities.size()!=methods_.readable_extents.size()||
    !plan_.old_classes.size()||plan_.old_classes.size()!=plan_.new_classes.size())return deny("boot-AOT-actual-stop-plan-or-owner-unproved");
 if(!TablesMapped(view,context))return deny("boot-AOT-current-table-extent-unproved");
 if(!EmptyRootMasks(view,row))return deny("boot-AOT-current-reference-roots-not-empty");
 if(view.InlineInfos().rows)return deny("boot-AOT-inline-dependency-closure-unproved");
 const auto*entry=catalog_.Lookup(method,roots_,context);
 if(!entry)return deny("boot-AOT-current-rooted-public-identity-unproved");
 if(entry->body!=native_boot_method_catalog::Body::kPrimitiveLocal)return deny("boot-AOT-bytecode-dependency-closure-unproved");
 if(entry->max_locals<=0||(entry->modifiers&0x508u)!=0x8u)return deny("boot-AOT-primitive-static-public-shape-unproved");
 if(range.first||range.count!=uint32_t(entry->max_locals))return deny("boot-AOT-complete-current-register-range-unproved");
 size_t extent=0;if(!context.extent(context.owner,method,&extent))return deny("boot-AOT-current-method-extent-unproved");
 const auto&layout=methods_.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 method_metadata::Identity identity;
 if(!method_metadata::ReadIdentity(layout,method,extent,&identity)||identity.dex_method_index==UINT32_MAX||
    (identity.access_flags&0x40508u)!=0x8u)return deny("boot-AOT-current-method-source-prefix-unproved"); // static, non-native, non-abstract, not obsolete
 void*declaring=metadata_.MethodDeclaringClass(method);
 constexpr size_t loader=offsetof(class_metadata::Class11,class_loader);
 if(!declaring||!context.extent(context.owner,declaring,&extent)||extent<loader+4||metadata_.Reference(declaring,loader))return deny("boot-AOT-current-boot-declaring-Class-unproved");
 field_copy::CopyClassView named;
 if(!metadata_.ClassIdentity(declaring,&named,nullptr,&context)||!named.descriptor||entry->declaring_signature!=named.descriptor)return deny("boot-AOT-current-declaring-descriptor-unproved");
 for(size_t i=0;i<plan_.old_classes.size();++i){
  if(declaring==plan_.old_classes[i]||declaring==plan_.new_classes[i])return deny("boot-AOT-declaring-Class-changed-by-plan");
  const auto prefix=native_boot_aot_prefix::Observe(plan_,i,metadata_,vtables_,context);
  if(!prefix.preserved)return deny(prefix.failure);
 }
 receiver_guard::CodeView source;
 if(!code_.Code(self_,method,&source,context))return deny("boot-AOT-current-source-CodeItem-unproved");
 if(!BodyMatches(*entry,source,identity.dex_method_index,pc,range,context))return deny("boot-AOT-source-public-body-DEX-or-PC-mismatch");
 if(!stop_.IsStopped(self_)||!roots_.FinalRootWindow()||catalog_.Lookup(method,roots_,context)!=entry)return deny("boot-AOT-final-stop-or-rooted-identity-changed");
 return true;
}
}
