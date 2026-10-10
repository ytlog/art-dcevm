#include "platform/common/compiled_frame_fixture.h"
#include "platform/common/compiled_frame_episode.h"
#include <cassert>
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <jni.h>
#include <cstring>
#include "platform/common/compiled_fixture_exception.h"
#include <atomic>
#include <string>
extern "C" jboolean Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_waitEntered(JNIEnv*,jclass,jint);
namespace {
std::atomic<bool> fail_wait{false};std::string diagnostic;
thread_local bool executor_active=false;
jboolean TestWait(JNIEnv*env,jclass type,jint timeout){if(executor_active){auto error=env->FindClass("java/lang/IllegalStateException");if(error)env->ThrowNew(error,"Owned public reflected Field identity unavailable: reentrant executor");return false;}if(!fail_wait)return Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixture_waitEntered(env,type,timeout);auto error=env->FindClass("java/lang/OutOfMemoryError");if(error)env->ThrowNew(error,"host start Throwable after worker creation");return false;}
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

extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_episode(JNIEnv*env,jclass,jobject run,jboolean old_scope,jint reject_inspection){
 auto type=env->GetObjectClass(run);auto start=env->GetMethodID(type,"start","()V");auto close=env->GetMethodID(type,"close","()V");env->DeleteLocalRef(type);
 std::string failure;bool inspected=false;size_t retained=0;
 executor_active=old_scope;
 const bool result=artdcevm::compiled_frame_fixture::RunEpisode(
  [&](std::string* error){env->CallVoidMethod(run,start);if(env->ExceptionCheck()){*error="start failed";return false;}return true;},
  [&](std::string* error){assert(!executor_active&&artdcevm::compiled_frame_fixture::OrdinaryBlockedTarget());
   // Original source boundary double: roots cannot survive this callback.
   executor_active=true;++retained;inspected=true;
   struct Leave{size_t&roots;~Leave(){--roots;executor_active=false;}}leave{retained};
   if(reject_inspection==2)throw std::string("source inspection exception");
   if(reject_inspection){*error="actual inspection refused";return false;}return true;},
  [&](std::string*){assert(retained==0);executor_active=false;
   jthrowable original=env->ExceptionOccurred();if(original)env->ExceptionClear();env->CallVoidMethod(run,close);
   if(original){if(env->ExceptionCheck())env->ExceptionClear();env->Throw(original);env->DeleteLocalRef(original);}
   return !env->ExceptionCheck();},&failure);
 assert(retained==0&&!executor_active&&!artdcevm::compiled_frame_fixture::OrdinaryBlockedTarget());
 assert(old_scope?!inspected:inspected);
 return result;
}
