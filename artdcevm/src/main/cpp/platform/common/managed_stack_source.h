#pragma once
#include "platform/abi/arm64/android16/thread.h"
#include "platform/common/modern_field_metadata.h"
#include <cstring>
namespace artdcevm::managed_stack_source {
// Fixed-source declaration facts only; mapping alone grants no thread identity,
// actual stop, native upcall or mutation permission.
struct Snapshot {
 const abi::arm64::android16::ManagedStack*address=nullptr;
 uintptr_t tagged_quick=0;const abi::arm64::android16::ManagedStack*link=nullptr;void*shadow=nullptr;
};
inline bool Read(const modern_field_metadata::ReadContext&context,
 const abi::arm64::android16::ManagedStack*fragment,Snapshot*out)noexcept {
 if(!out||!fragment||uintptr_t(fragment)%alignof(abi::arm64::android16::ManagedStack)||sizeof(*fragment)>UINTPTR_MAX-uintptr_t(fragment)||!context.owner||!context.extent)return false;
 size_t extent=0;if(!context.extent(context.owner,fragment,&extent)||extent<sizeof(*fragment))return false;
 abi::arm64::android16::ManagedStack value;std::memcpy(&value,fragment,sizeof(value));
 const auto tagged=value.tagged_top_quick_frame_.tagged_sp_;const auto quick=tagged&~uintptr_t{3};
 if((tagged&3u)==3u||(tagged&&!quick)||(quick&&quick%16)||(quick&&value.top_shadow_frame_)||
    (value.link_&&(uintptr_t(value.link_)%alignof(abi::arm64::android16::ManagedStack)||sizeof(*fragment)>UINTPTR_MAX-uintptr_t(value.link_))))return false;
 *out={fragment,tagged,value.link_,value.top_shadow_frame_};return true;
}
inline bool Same(const Snapshot&a,const Snapshot&b)noexcept {
 return a.address==b.address&&a.tagged_quick==b.tagged_quick&&a.link==b.link&&a.shadow==b.shadow;
}
// Structural chain census, including empty fragments. Floyd's complete cycle
// check has no fixed cap and no allocation. This remains facts, not permission.
struct Census {size_t fragments=0;const abi::arm64::android16::ManagedStack*terminal=nullptr;bool complete=false;};
inline Census Count(const modern_field_metadata::ReadContext&read,
 const abi::arm64::android16::ManagedStack*head)noexcept {
 Census result;auto*cursor=head;auto*slow=head;auto*fast=head;
 while(cursor){Snapshot value;if(!Read(read,cursor,&value)||result.fragments==SIZE_MAX)return result;
  ++result.fragments;result.terminal=cursor;cursor=value.link;
  if(slow){Snapshot step;if(!Read(read,slow,&step))return result;slow=step.link;}
  for(unsigned i=0;i<2&&fast;++i){Snapshot step;if(!Read(read,fast,&step))return result;fast=step.link;}
  if(fast&&fast==slow)return result;
 }
 result.complete=head!=nullptr;return result;
}
}
