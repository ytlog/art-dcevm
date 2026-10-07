#include "platform/android13/thread_suspend_scope_abi.h"
#include "thread.h"
#include "thread_list.h"
#include "scoped_thread_state_change-inl.h"
namespace fixed=artdcevm::android13::thread_suspend_abi;
static_assert(sizeof(art::ScopedSuspendAll)==sizeof(fixed::SuspendAll));
static_assert(alignof(art::ScopedSuspendAll)==alignof(fixed::SuspendAll));
static_assert(art::Thread::ThreadFlagsOffset<art::PointerSize::k64>().Int32Value()==
    artdcevm::android13::native_access_abi::kThreadFlagsOffset);
static_assert(static_cast<uint32_t>(art::ThreadState::kNative)==artdcevm::android13::native_access_abi::kNative);
static_assert(static_cast<uint32_t>(art::ThreadState::kRunnable)==artdcevm::android13::native_access_abi::kRunnable);
extern "C" bool ArtDcevmSourceProbeThreadSuspendScopes(bool(*calibrate)(void*,bool(*)(void*))) {
  if(art::kIsDebugBuild || !calibrate) return false;
  art::ScopedObjectAccess soa(art::Thread::Current());
  auto is_native=+[](void* raw) {return static_cast<art::Thread*>(raw)->GetState()==art::ThreadState::kNative;};
  return calibrate(soa.Self(),is_native) && soa.Self()->GetState()==art::ThreadState::kRunnable;
}
