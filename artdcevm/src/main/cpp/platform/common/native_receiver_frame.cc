#include "platform/common/native_receiver_frame.h"
#include <cstring>
namespace artdcevm::receiver_guard {
namespace {
// mirror-independent fixed interpreter/shadow_frame.h source declarations.
struct LegacyFrame {void*link;void*method;void*result;void*dex_pc;void*instructions;void*monitors;uint32_t registers,pc;int16_t cached_hotness,hotness;uint32_t flags;uint32_t vregs[1];};
struct CompactFrame {void*link;void*method;void*monitors;uint32_t registers,pc,flags;uint32_t vregs[1];};
static_assert(offsetof(LegacyFrame,registers)==48&&offsetof(LegacyFrame,vregs)==64);
static_assert(offsetof(CompactFrame,registers)==24&&offsetof(CompactFrame,vregs)==36);
constexpr size_t counts[]={offsetof(LegacyFrame,registers),offsetof(CompactFrame,registers)};
constexpr size_t vregs[]={offsetof(LegacyFrame,vregs),offsetof(CompactFrame,vregs)};
}
bool FrameReader::Bounds(void*self,const void*frame,const InstructionOperands*operands,size_t count_offset,size_t data_offset,uint32_t*reg,uint32_t*reference)const noexcept{
 if(!self||!frame||!operands||!access_.current||!access_.extent||!access_.reference||!access_.current(access_.owner,self))return false;
 size_t frame_extent=0,operand_extent=0;
 if(!access_.extent(access_.owner,self,frame,&frame_extent)||frame_extent<data_offset||!access_.extent(access_.owner,self,operands,&operand_extent)||operand_extent<sizeof(InstructionOperands)||!operands->Count())return false;
 uint32_t count=0;std::memcpy(&count,static_cast<const char*>(frame)+count_offset,4);
 const uint32_t index=operands->GetOperand(0);
 // References follow the raw vreg array at offsetof(vregs), not sizeof(Frame).
 // Validate the complete two-part source range before touching its root slot.
 if(index>=count||count>(frame_extent-data_offset)/8)return false;
 std::memcpy(reference,static_cast<const char*>(frame)+data_offset+size_t(count+uint64_t(index))*4,4);
 *reg=index;return access_.current(access_.owner,self);
}
bool FrameReader::Observe(const void*frame,const InstructionOperands*operands,const FrameOracle&oracle)noexcept{
 if(sealed_||!oracle.thread||!oracle.method||!oracle.receiver||uintptr_t(oracle.receiver)>UINT32_MAX)return false;
 unsigned next=candidates_;
 for(unsigned i=0;i<2;++i)if(next&(1u<<i)){uint32_t reg=0,reference=0;void*method=nullptr;
  if(!Bounds(oracle.thread,frame,operands,counts[i],vregs[i],&reg,&reference)){next&=~(1u<<i);continue;}
  std::memcpy(&method,static_cast<const char*>(frame)+8,8);
  // Compare the independently rooted identity before a barrier touches any
  // candidate slot. A mismatched candidate's arbitrary bits are never decoded.
  if(method!=oracle.method||reg!=oracle.receiver_register||reference!=uint32_t(uintptr_t(oracle.receiver)))next&=~(1u<<i);
 }
 if(!next)return false;candidates_=next;observed_=true;return true;
}
bool FrameReader::Seal()noexcept{if(!observed_||(candidates_!=1&&candidates_!=2))return false;sealed_=true;return true;}
bool FrameReader::Read(void*self,const void*frame,const InstructionOperands*operands,void**receiver)const noexcept{
 if(!sealed_||!receiver)return false;uint32_t reg=0,reference=0;const unsigned i=candidates_==1?0:1;
 if(!Bounds(self,frame,operands,counts[i],vregs[i],&reg,&reference))return false;
 void*value=nullptr;if(reference&&(!access_.reference(access_.owner,self,reference,&value)||!value))return false;
 if(!access_.current(access_.owner,self))return false;*receiver=value;return true;
}
}
