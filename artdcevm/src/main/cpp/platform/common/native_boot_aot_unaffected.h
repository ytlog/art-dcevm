#pragma once
#include "platform/common/code_info_view.h"
#include "platform/common/decoded_frame_recovery.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/native_boot_method_catalog.h"
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/native_boot_dex_identity.h"
#include "platform/common/native_boot_return_continuation.h"
namespace artdcevm::native_boot_aot_unaffected {
class Source;
// Only the concrete source dependency owner can construct this interface. It
// cannot be replaced with a bool callback or an async eligibility override.
class Proof {
 public:
 Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 virtual ~Proof()=default;
 virtual bool Verify(void* method,uint32_t dex_pc,const code_info_view::View&,
                     uint32_t stack_row,const decoded_frame_recovery::RegisterRange&,
                     const modern_field_metadata::ReadContext&,const char**failure=nullptr)const noexcept=0;
 virtual bool InspectContinuation(void*,uint32_t,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  receiver_guard::CodeView*,native_boot_return_continuation::Shape*,const char**)const noexcept=0;
 virtual bool InspectExceptionalEntry(void*,uint32_t,const modern_field_metadata::ReadContext&,
  native_boot_return_continuation::HandlerStorage,native_boot_return_continuation::Scratch,
  receiver_guard::CodeView*,native_boot_return_continuation::Shape*,const char**)const noexcept=0;
 virtual bool CurrentCalleeCode(void*,const modern_field_metadata::ReadContext&,receiver_guard::CodeView*)const noexcept=0;
 private:
 friend class Source;Proof()=default;
};
// Fixed StackMap source columns 3/4 are register/stack GC mask indices. kNoValue
// means zero/empty respectively in original CodeInfo::Get*MaskOf. This stronger
// empty-root subset is deliberate: primitive static bodies need no references.
inline bool EmptyRootMasks(const code_info_view::View&view,uint32_t row)noexcept{
 uint32_t registers=0,stack=0;
 return view.StackMaps().Read(row,3,&registers)&&view.StackMaps().Read(row,4,&stack)&&
        registers==UINT32_MAX&&stack==UINT32_MAX;
}
inline bool TablesMapped(const code_info_view::View&view,const modern_field_metadata::ReadContext&context)noexcept{
 if(!context.owner||!context.extent)return false;
 for(const auto&table:view.tables){
  if(!table.region.bit_size)continue;
  const size_t start=table.region.bit_start;
  if(table.region.bit_size>SIZE_MAX-start||start+table.region.bit_size>SIZE_MAX-7)return false;
  const size_t needed=(start+table.region.bit_size+7)/8;size_t extent=0;
  if(!table.region.data||!context.extent(context.owner,table.region.data,&extent)||extent<needed)return false;
 }
 return true;
}
// Pure immutable body/DEX identity check, not a stop or dependency authority.
// Source performs Class/root/prefix validation before calling this predicate.
inline bool ImmutableBodyMatches(const native_boot_method_catalog::Method&entry,
 const receiver_guard::CodeView&source,uint32_t method_index,const modern_field_metadata::ReadContext&context)noexcept{
 if(entry.max_locals<=0||!source.instructions||source.units!=entry.dex.size()/2||
    source.registers!=uint32_t(entry.max_locals)||!context.owner||!context.extent)return false;
 size_t extent=0;
 if(!context.extent(context.owner,source.instructions,&extent)||extent<entry.dex.size()||
    std::memcmp(source.instructions,entry.dex.data(),entry.dex.size()))return false;
 const auto&dex=source.source_dex;
 if(!dex.begin||!dex.data_begin||!context.extent(context.owner,dex.begin,&extent)||extent<dex.size||
    !context.extent(context.owner,dex.data_begin,&extent)||extent<dex.data_size)return false;
 return DexIdentity(dex).Matches(method_index,entry.declaring_signature,entry.name,entry.descriptor);
}
inline bool BodyMatches(const native_boot_method_catalog::Method&entry,
 const receiver_guard::CodeView&source,uint32_t method_index,uint32_t pc,
 const decoded_frame_recovery::RegisterRange&range,const modern_field_metadata::ReadContext&context)noexcept{
 if(entry.body!=native_boot_method_catalog::Body::kPrimitiveLocal||entry.max_locals<=0||
    (entry.modifiers&0x508u)!=8u||range.first||range.count!=uint32_t(entry.max_locals)||
    pc>=source.units||!ImmutableBodyMatches(entry,source,method_index,context))return false;
 if(native_boot_method_catalog::Classify(entry.dex.data(),entry.dex.size(),entry.descriptor.c_str(),pc)!=native_boot_method_catalog::Body::kPrimitiveLocal)return false;
 return true;
}
}
