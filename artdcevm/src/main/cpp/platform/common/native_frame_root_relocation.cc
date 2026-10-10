#include "platform/common/native_frame_root_relocation.h"
#include "platform/abi/arm64/stack_visitor.h"
namespace artdcevm::native_frame_root_relocation {
bool Proof::Root(const native_typed_heap_walk::RootRecord&root)noexcept{
 if(!Owner()||(!capturing_&&!after_)||result_.failure!=Failure::None)return Deny(Failure::Owner);
 using namespace abi::arm64::runtime_root_visit;
 if(root.type!=kJavaFrameRoot)return true;
 if(!root.original_frame_visitor)return Deny(Failure::OriginalCursor);
 // The common fixed StackVisitor fields through header are identical across
 // source families. No decoded CodeInfo, source vptr or private array offsets
 // are inspected. The source RootInfo/visitor object exists only this callback.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
 const auto*source=static_cast<const unsigned char*>(root.original_frame_visitor);
 constexpr size_t extent=offsetof(abi::arm64::stack::Visitor11,header)+sizeof(void*);
 if(!Mapped(source,extent))return Deny(Failure::Memory);
 void* thread=nullptr;void**quick=nullptr;const void*header=nullptr;uintptr_t pc=0;
 std::memcpy(&thread,source+offsetof(abi::arm64::stack::Visitor11,thread),sizeof(thread));
 std::memcpy(&quick,source+offsetof(abi::arm64::stack::Visitor11,quick_frame),sizeof(quick));
 std::memcpy(&header,source+offsetof(abi::arm64::stack::Visitor11,header),sizeof(header));
 std::memcpy(&pc,source+offsetof(abi::arm64::stack::Visitor11,pc),sizeof(pc));
#pragma clang diagnostic pop
 SavedFrame*frame=nullptr;
 for(auto&candidate:frames_)if(candidate.key.thread==thread&&candidate.key.quick==quick){frame=&candidate;break;}
 if(!frame)return true; // Unrequested frames grant no proof to this owner.
 if(!root.thread_id||(frame->key.thread_id&&frame->key.thread_id!=root.thread_id)||frame->key.header!=header||frame->key.pc!=pc)return Deny(Failure::OriginalCursor);
 frame->key.thread_id=root.thread_id;
 // Independently consult the ORIGINAL visitor Context, including null GPR
 // mask bits which do not emit a root callback. A Context scratch/zero slot
 // cannot pass by simply containing null: its address must be the same real
 // saved spill seen by the independent source visitor.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
 size_t context_at=offsetof(abi::arm64::stack::Visitor13,context);
 if(frame->key.family==code_info_view::SourceFamily::kAndroid11)context_at=offsetof(abi::arm64::stack::Visitor11,context);
 else if(frame->key.family==code_info_view::SourceFamily::kAndroid12)context_at=offsetof(abi::arm64::stack::Visitor12,context);
 else if(frame->key.family==code_info_view::SourceFamily::kAndroid17)context_at=offsetof(abi::arm64::stack::Visitor17,context);
#pragma clang diagnostic pop
 abi::arm64::context::Prefix*original_context=nullptr;
 if(!Mapped(source+context_at,sizeof(original_context)))return Deny(Failure::Memory);
 std::memcpy(&original_context,source+context_at,sizeof(original_context));
 if(!Mapped(original_context,sizeof(*original_context)))return Deny(Failure::OriginalCursor);
 for(size_t index=frame->first;index<frame->limit;++index){const auto&gpr=slots_[index];if(!gpr.pointer)continue;
  if(!original_context->IsAccessibleGPR(gpr.reg)||original_context->GetGPRAddress(gpr.reg)!=gpr.address)return Deny(Failure::OriginalCursor);
 }
 frame->origin_seen=true;
 if(root.vreg==kDeclaringClassVreg||root.vreg==kProxyReferenceArgumentVreg||root.vreg==kNativeReferenceArgumentVreg)return true; // Source method root, not GC mask.
 if(root.vreg!=kImpreciseVreg||root.representation!=native_typed_heap_walk::RootRepresentation::Pointer)return Deny(Failure::OriginalOrder);
 SkipNull(*frame);if(frame->next==frame->limit)return Deny(Failure::OriginalOrder);
 const auto&slot=slots_[frame->next];void*physical=nullptr;
 if(!ReadSlot(slot,&physical))return Deny(Failure::Memory);
 const auto expected=after_?Replacement(slot.before):slot.before;
 if(physical!=expected||root.value!=expected)return Deny(after_?Failure::RelocatedValue:Failure::OriginalValue);
 // Pointer register roots are the original source's actual saved spill. Stack
 // roots instead use source temporary &new_ref; ordered exact-frame GC-mask
 // index determines the real compressed slot, never a search by root value.
 if(slot.pointer&&root.slot!=slot.address)return Deny(Failure::OriginalOrder);
 ++frame->next;++result_.callbacks;return true;
}
}
