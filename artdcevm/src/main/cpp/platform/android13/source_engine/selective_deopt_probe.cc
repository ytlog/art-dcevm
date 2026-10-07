#include "platform/android13/selective_deopt_abi.h"
#include "runtime.h"
#include "thread.h"
#include "thread-current-inl.h"
#include "instrumentation.h"
#include "deoptimization_kind.h"
#include <map>
#include <cstring>
#include <android/log.h>
namespace art {
struct SelectiveLayoutProbe {
  static void Verify() {
    namespace a=artdcevm::android13::selective_abi;
    static_assert(offsetof(Runtime,instrumentation_)==offsetof(a::RuntimePrefix,instrumentation));
    static_assert(offsetof(Thread,tlsPtr_.instrumentation_stack)==a::kThreadInstrumentationStack);
    static_assert(Thread::InterpreterCacheOffset<PointerSize::k64>().Int32Value()==2608);
    static_assert(sizeof(RuntimeStats)==sizeof(a::RuntimeStats));
    static_assert(alignof(RuntimeStats)==alignof(a::RuntimeStats));
    static_assert(sizeof(std::map<uintptr_t,instrumentation::InstrumentationStackFrame>)==sizeof(a::Tree));
    static_assert(sizeof(instrumentation::InstrumentationStackFrame)==sizeof(a::Frame));
    static_assert(offsetof(instrumentation::InstrumentationStackFrame,force_deopt_id_)==offsetof(a::Frame,force_deopt_id));
    static_assert(static_cast<uint8_t>(DeoptimizeFlagValue::kDebug)==a::kDebugFlag);
  }
};
}
extern "C" bool ArtDcevmSourceProbeSelectiveDeopt(
    artdcevm::android13::selective_abi::Frame* (*find)(void*,uintptr_t),
    void* runtime,void* instrumentation) {
  art::SelectiveLayoutProbe::Verify();
  if (runtime!=art::Runtime::Current() || instrumentation!=art::Runtime::Current()->GetInstrumentation()) { __android_log_print(ANDROID_LOG_ERROR,"ArtDcevmSelective","runtime=%p expected=%p instrumentation=%p expected=%p",runtime,art::Runtime::Current(),instrumentation,art::Runtime::Current()->GetInstrumentation());return false; }
  std::map<uintptr_t,art::instrumentation::InstrumentationStackFrame> frames;
  for (uintptr_t key:{7,3,11,1,5,9,13}) frames.emplace(key,art::instrumentation::InstrumentationStackFrame(nullptr,nullptr,key,false,key));
  for (auto& entry:frames) {
    auto* frame=find(&frames,entry.first);
    if (reinterpret_cast<void*>(frame)!=&entry.second || frame->force_deopt_id!=entry.first) { __android_log_print(ANDROID_LOG_ERROR,"ArtDcevmSelective","key=%llu found=%p expected=%p map=%p",static_cast<unsigned long long>(entry.first),frame,&entry.second,&frames);return false; }
    const uint64_t sentinel=UINT64_MAX;
    std::memcpy(reinterpret_cast<unsigned char*>(frame)+offsetof(artdcevm::android13::selective_abi::Frame,force_deopt_id),&sentinel,sizeof(sentinel));
    if (entry.second.force_deopt_id_!=UINT64_MAX) { __android_log_print(ANDROID_LOG_ERROR,"ArtDcevmSelective","sentinel did not update platform frame");return false; }
  }
  return find(&frames,0)==nullptr && find(&frames,15)==nullptr;
}
