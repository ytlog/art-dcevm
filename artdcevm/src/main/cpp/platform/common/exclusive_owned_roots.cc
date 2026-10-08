#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include <vector>
namespace artdcevm::exclusive_owned_roots {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
constexpr uint32_t kNative=92,kModernRunnable=0,kLegacyRunnable=67;
}
struct Service::Request {
  const Service* service;const jobject* globals;size_t count;
  void* self;unsigned shift;uint32_t runnable;std::string* error;
};
bool Service::Bind(HookEngine& hooks,Service* output,std::string* error) {
  if(!output)return Fail(error,"Missing exclusive-root service output");
  Service candidate;
  candidate.current_=reinterpret_cast<decltype(candidate.current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
  candidate.suspend_=reinterpret_cast<decltype(candidate.suspend_)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendConstruct));
  candidate.resume_=reinterpret_cast<decltype(candidate.resume_)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendDestroy));
  // These fixed source declarations return the same release ObjPtr ABI. The
  // executor independently verifies the selected decoder against owned JNI
  // identities before any exclusive use. No JNI transition entrypoint is used.
  void* decoder=hooks.FindSymbol("libart.so","_ZNK3art6Thread13DecodeJObjectEP8_jobject");
  if(!decoder)decoder=hooks.FindSymbol("libart.so",runnable_jni::GlobalDecoder::SymbolName());
  if(!candidate.current_||!candidate.suspend_||!candidate.resume_||!decoder)
    return Fail(error,"Missing complete native exclusive-root named capabilities");
  candidate.decode_=reinterpret_cast<Decode>(decoder);*output=candidate;
  if(error)error->clear();return true;
}
bool Service::State(void* self,unsigned shift,uint32_t* output)const {
  size_t extent=0;
  if(!self||!output||!method_identity::ReadableMappingExtent(self,&extent)||extent<4)return false;
  const auto* flags=static_cast<const uint32_t*>(self);
  *output=__atomic_load_n(flags,__ATOMIC_RELAXED)>>shift;return true;
}
bool Service::Enter(const runnable_jni::Context& scope,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);const Service& service=*request.service;
  uint32_t state=0;
  if(scope.self!=request.self||service.current_()!=scope.self||
      !service.State(scope.self,request.shift,&state)||state!=request.runnable)
    return Fail(request.error,"Actual Runnable callback disagrees with owned Native state family");
  for(size_t i=0;i<request.count;++i) {
    void* checked=nullptr;
    if(!runnable_executor::DecodeStrongGlobal(scope,request.globals[i],&checked)||!checked||
        reinterpret_cast<void*>(service.decode_(scope.self,request.globals[i]).reference)!=checked)
      return Fail(request.error,"Owned Runnable global identity did not match named decoder");
  }
  return true;
}
bool Service::With(JNIEnv* env,const jobject* globals,size_t count,Operation operation,void* data,std::string* error)const {
  if(error)error->clear();
  if(!env||!globals||!count||count>128||!operation||!current_||env->ExceptionCheck())
    return Fail(error,"Invalid owned exclusive-root request");
  for(size_t i=0;i<count;++i)
    if(!globals[i]||env->GetObjectRefType(globals[i])!=JNIGlobalRefType||env->ExceptionCheck())
      return Fail(error,"Exclusive roots require independently owned strong globals");
  void* self=current_();uint32_t state=0;unsigned shift=24;uint32_t runnable=kModernRunnable;
  // Select only a state-word candidate, and require its independently observed
  // Runnable counterpart. No SDK or ELF provenance grants this capability.
  if(!State(self,shift,&state))return Fail(error,"Native JNI state word is unavailable");
  if(state!=kNative) {
    shift=16;runnable=kLegacyRunnable;
    if(!State(self,shift,&state)||state!=kNative)return Fail(error,"Owned JNI boundary is not a supported Native state");
  }
  Request request{this,globals,count,self,shift,runnable,error};
  const auto result=runnable_executor::With(env,globals[0],Enter,&request);
  // The executor has fully released its shared mutator scope before SuspendAll.
  // Check restoration even when the calibration operation rejects or throws.
  if(current_()!=self||!State(self,shift,&state)||state!=kNative)
    return Fail(error,"Runnable calibration did not restore original Native JNI state");
  if(result!=runnable_jni::Result::kSuccess)
    return error&&!error->empty()?false:Fail(error,"Owned exclusive-root calibration failed");
  // Allocation and JNI remain outside the stop. No borrowed decoded pointer
  // crosses this possible GC boundary: every root is reloaded under the stop.
  std::vector<void*> roots(count);
  bool completed=false;
  try {
    abi::arm64::android_common::SuspendAll storage{};
    suspend_(&storage,"artdcevm owned exclusive roots",false);
    struct Release {abi::arm64::android_common::SuspendDestroy resume;void* storage;
      ~Release(){resume(storage);}} release{resume_,&storage};
    for(size_t i=0;i<count;++i) {
      roots[i]=reinterpret_cast<void*>(decode_(self,globals[i]).reference);
      if(!roots[i])return Fail(error,"Owned global is empty under exclusive mutator access");
    }
    completed=operation({self,roots.data(),roots.size()},data);
  } catch(...) {
    return Fail(error,"Owned exclusive operation threw; native stop released");
  }
  if(current_()!=self||!State(self,shift,&state)||state!=kNative)
    return Fail(error,"Owned exclusive scope did not preserve original Native JNI state");
  return completed?true:Fail(error,"Owned exclusive operation rejected; native stop released");
}
}
