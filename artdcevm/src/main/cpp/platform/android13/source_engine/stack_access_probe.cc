#include "platform/android13/stack_access_abi.h"
#include "stack.h"
#include "dex/dex_file.h"
#include "oat_quick_method_header.h"
#include "scoped_thread_state_change-inl.h"
#include "thread-current-inl.h"
namespace art {
struct StackLayoutProbe {
  static void Verify() {
    using F=artdcevm::android13::stack_abi::Visitor;
    static_assert(sizeof(F)==sizeof(StackVisitor));
    static_assert(alignof(F)==alignof(StackVisitor));
#define MATCH(f,s) static_assert(offsetof(F,f)==offsetof(StackVisitor,s))
    MATCH(thread,thread_); MATCH(kind,walk_kind_); MATCH(shadow_frame,cur_shadow_frame_);
    MATCH(quick_frame,cur_quick_frame_); MATCH(pc,cur_quick_frame_pc_);
    MATCH(header,cur_oat_quick_method_header_); MATCH(num_frames,num_frames_);
    MATCH(depth,cur_depth_); MATCH(inline_frames,current_inline_frames_);
    MATCH(inline_info,cur_inline_info_); MATCH(stack_map,cur_stack_map_);
    MATCH(context,context_); MATCH(check_suspended,check_suspended_);
#undef MATCH
    static_assert(sizeof(artdcevm::android13::stack_abi::CodeInfo)==sizeof(CodeInfo));
    static_assert(offsetof(DexFile,oat_dex_file_)==offsetof(artdcevm::android13::stack_abi::DexPrefix,oat_dex));
    static_assert(sizeof(OatQuickMethodHeader)==artdcevm::android13::stack_abi::kCodeOffset);
    static_assert(static_cast<int>(StackVisitor::StackWalkKind::kSkipInlinedFrames)==1);
  }
};
}
extern "C" bool ArtDcevmSourceProbeStackAccess(void (*walk)(void*)) {
  art::StackLayoutProbe::Verify();
  art::ScopedObjectAccess soa(art::Thread::Current());
  walk(soa.Self());
  return true;
}
