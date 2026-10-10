#include "platform/common/compiled_frame_fixture.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <jni.h>
#include <cstring>
#include "platform/common/compiled_fixture_exception.h"
#include <atomic>
#include <string>
extern "C" jboolean Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_waitEntered(JNIEnv*,jclass,jint);
namespace {
std::atomic<bool> fail_wait{false};std::string diagnostic;
jboolean TestWait(JNIEnv*env,jclass type,jint timeout){if(!fail_wait)return Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_waitEntered(env,type,timeout);auto error=env->FindClass("java/lang/OutOfMemoryError");if(error)env->ThrowNew(error,"host start Throwable after worker creation");return false;}
void* Current(){static thread_local int native_thread;return &native_thread;}
class Hooks final:public artdcevm::HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char* lib,const char* symbol)override{return !std::strcmp(lib,"libart.so")&&!std::strcmp(symbol,artdcevm::abi::arm64::android11::kCurrentThread)?reinterpret_cast<void*>(Current):nullptr;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}
 bool Remove(void*,std::string*)override{return false;}
};
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_initialize(JNIEnv*env,jclass){Hooks hooks;auto fixture=env->FindClass("com/github/ytlog/artdcevm/internal/CompiledFrameFixture");JNINativeMethod method{const_cast<char*>("waitEntered"),const_cast<char*>("(I)Z"),reinterpret_cast<void*>(TestWait)};if(!fixture||env->RegisterNatives(fixture,&method,1)!=JNI_OK)return false;env->DeleteLocalRef(fixture);return artdcevm::compiled_frame_fixture::Initialize(hooks);}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_blocked(JNIEnv*,jclass){return artdcevm::compiled_frame_fixture::OrdinaryBlockedTarget()!=nullptr;}

extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_failWait(JNIEnv*,jclass,jboolean value){fail_wait=value;}
extern "C" JNIEXPORT void JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_describe(JNIEnv*env,jclass,jthrowable original){env->Throw(original);artdcevm::compiled_frame_fixture::DescribePending(env,"compiled-fixture.prepare.warmup",&diagnostic);}
extern "C" JNIEXPORT jstring JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_diagnostic(JNIEnv*env,jclass){return env->NewStringUTF(diagnostic.c_str());}
