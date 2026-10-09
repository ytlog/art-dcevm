#include "platform/common/compiled_frame_fixture.h"
#include "platform/abi/arm64/android11/jni_current_thread.h"
#include <jni.h>
#include <cstring>
namespace {
void* Current(){static thread_local int native_thread;return &native_thread;}
class Hooks final:public artdcevm::HookEngine {
 bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char* lib,const char* symbol)override{return !std::strcmp(lib,"libart.so")&&!std::strcmp(symbol,artdcevm::abi::arm64::android11::kCurrentThread)?reinterpret_cast<void*>(Current):nullptr;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}
 bool Remove(void*,std::string*)override{return false;}
};
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_initialize(JNIEnv*,jclass){Hooks hooks;return artdcevm::compiled_frame_fixture::Initialize(hooks);}
extern "C" JNIEXPORT jboolean JNICALL Java_com_github_ytlog_artdcevm_internal_CompiledFrameFixtureHostTest_blocked(JNIEnv*,jclass){return artdcevm::compiled_frame_fixture::OrdinaryBlockedTarget()!=nullptr;}
