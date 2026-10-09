#include "platform/common/compiled_frame_fixture.h"
#include "platform/common/owned_active_frame_gate.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <atomic>
#include <jni.h>
namespace artdcevm::compiled_frame_fixture {
namespace {
owned_active_frame::Gate gate;
std::atomic<abi::arm64::android11::CurrentThread> current{nullptr};
}
bool Initialize(HookEngine& hooks)noexcept {
 auto leaf=reinterpret_cast<abi::arm64::android11::CurrentThread>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
 if(!leaf)return false;auto expected=static_cast<abi::arm64::android11::CurrentThread>(nullptr);
 return current.compare_exchange_strong(expected,leaf)||expected==leaf;
}
void* OrdinaryBlockedTarget()noexcept{return gate.Target();}
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_awaitGate(JNIEnv* env,jclass) {
 using namespace artdcevm::compiled_frame_fixture;
 const auto leaf=current.load();
 if(!leaf||!gate.Await(leaf())){
  auto error=env->FindClass("java/lang/IllegalStateException");
  if(error)env->ThrowNew(error,"Compiled fixture bridge is missing or native target ownership changed");
 }
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_armGate(JNIEnv*,jclass) {
 using namespace artdcevm::compiled_frame_fixture;const auto leaf=current.load();
 return leaf&&leaf()&&gate.Arm()?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_waitEntered(JNIEnv*,jclass,jint timeout) {
 return timeout>0&&timeout<=30000&&artdcevm::compiled_frame_fixture::gate.Entered(static_cast<unsigned>(timeout))?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_releaseGate(JNIEnv*,jclass){artdcevm::compiled_frame_fixture::gate.Release();}
