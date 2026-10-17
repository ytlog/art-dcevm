#pragma once
#include <cstdint>
#include "platform/abi/arm64/common/root_visitor.h"

namespace artdcevm::abi::arm64 {
// Fixed Thread::HandleScopeVisitRoots: pid_t/int32_t in Android 11,
// uint32_t in Android 12--17. Keep each named service's exact prototype.
struct HandleScopeVisitors {
  using Signed = void (*)(void*, RootVisitor*, int32_t);
  using Unsigned = void (*)(void*, RootVisitor*, uint32_t);
  Signed signed_ids = nullptr;
  Unsigned unsigned_ids = nullptr;
  bool Available() const { return signed_ids || unsigned_ids; }
  bool Visit(bool signed_thread_id, void* thread, RootVisitor* roots, uint32_t tid) const {
    if (signed_thread_id) {
      if (!signed_ids) return false;
      signed_ids(thread, roots, static_cast<int32_t>(tid));
    } else {
      if (!unsigned_ids) return false;
      unsigned_ids(thread, roots, tid);
    }
    return true;
  }
};
}  // namespace artdcevm::abi::arm64
