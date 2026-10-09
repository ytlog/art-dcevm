#include "platform/common/jni_receiver_hook_lifetime.h"
#include "hooks/shadow_hook_engine.h"
#include <cstdio>
#include <cinttypes>
#include <dlfcn.h>
static_assert(sizeof(void*)==4);
namespace {
using Installer=artdcevm::jni_receiver_hook_lifetime::Installer<4>;
// Pin the dispatcher image (and its ShadowHook dependency) before installing a
// process-lifetime hook. Never release these objects or the retained handle.
void LifetimeAnchor() {}
bool Pin(void*,std::string* error) {
  Dl_info info{};
  if(!dladdr(reinterpret_cast<void*>(LifetimeAnchor),&info) || !info.dli_fname) {
    if(error)*error="Dispatcher image unavailable";return false;
  }
  static void* retained=dlopen(info.dli_fname,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
  if(!retained){if(error)*error="Dispatcher lifetime pin refused";return false;}
  return true;
}
bool Extent(const void* pointer,size_t* output) {
  const uintptr_t address=reinterpret_cast<uintptr_t>(pointer);
  FILE* maps=std::fopen("/proc/self/maps","r");if(!maps)return false;
  char line[1024];bool found=false;
  while(std::fgets(line,sizeof(line),maps)) {
    uintptr_t start=0,end=0;char permissions[5]{};
    if(std::sscanf(line,"%" SCNxPTR "-%" SCNxPTR " %4s",&start,&end,permissions)==3 &&
        permissions[0]=='r' && start<=address && address<end) {
      *output=end-address;found=true;break;
    }
  }
  std::fclose(maps);return found;
}
std::string Quote(const std::string& text) {
  std::string result="\"";
  for(unsigned char c:text) {
    if(c=='"' || c=='\\'){result+='\\';result+=static_cast<char>(c);}
    else if(c<32)result+='?';else result+=static_cast<char>(c);
  }
  return result+'"';
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_github_ytlog_artdcevm_testing_Arm32ReceiverProbe_nativeCalibrate(
    JNIEnv* env,jclass,jobject carrier) {
  // This ordinary JNI declaration has no FastNative/CriticalNative annotation.
  if(!env || env->ExceptionCheck())return nullptr;
  static auto* hooks=new artdcevm::ShadowHookEngine;
  static auto* installer=new Installer;
  size_t extent=0;std::string error;
  artdcevm::jni_receiver_calibration::Report observation{
      artdcevm::jni_receiver_calibration::Result::kUnavailable,
      artdcevm::jni_receiver_calibration::CallbackResult::kUnavailable};
  const bool passed=Extent(env,&extent) &&
      installer->InitializeFromNative(*hooks,env,carrier,extent,Pin,nullptr,&error,&observation);
  if(error.empty() && !passed)error="Current JNIEnv mapping unavailable";
  if(env->ExceptionCheck())return nullptr;
  std::string report="{\"passed\":"+std::string(passed?"true":"false")+
      ",\"hookInstalled\":"+(installer->Bound()?"true":"false")+
      ",\"receiverReady\":"+(installer->Ready()?"true":"false")+
      ",\"nativeOriginalJNIReceiverReady\":"+(passed && observation.original_callback_hits==1 && observation.matched_owner_prefixes==2?"true":"false")+
      ",\"jniPrefixReadable\":"+(passed && extent>=sizeof(artdcevm::abi::common::JniEnvironmentPrefix)?"true":"false")+
      ",\"pointerBytes\":"+std::to_string(sizeof(void*))+
      ",\"runnableScopeBytes\":"+std::to_string(sizeof(artdcevm::abi::common::RunnableJniScope))+
      ",\"jniMappedBytes\":"+std::to_string(extent)+
      ",\"originalCallbackHits\":"+std::to_string(observation.original_callback_hits)+
      ",\"matchedOwnerPrefixes\":"+std::to_string(observation.matched_owner_prefixes)+
      ",\"privateEvolutionInitialized\":false,\"publicJvmtiInitialized\":false"+
      ",\"diagnostic\":"+Quote(error)+"}";
  return env->NewStringUTF(report.c_str());
}
