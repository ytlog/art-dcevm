#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android13/thread_state.h"
#include <vector>
namespace artdcevm::exclusive_owned_roots {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
constexpr uint32_t kNative=92,kRunnable=0;
}
struct Service::Request {
  const Service* service;const jobject* globals;size_t count;
  Operation operation;void* data;void* self;std::string* error;
};
bool Service::Bind(HookEngine& hooks,Service* output,std::string* error) {
  if(!output)return Fail(error,"Missing exclusive-root service output");
  Service candidate;
  candidate.current_=reinterpret_cast<decltype(candidate.current_)>(hooks.FindSymbol("libart.so",abi::arm64::android11::kCurrentThread));
  candidate.native_=reinterpret_cast<Transition>(hooks.FindSymbol("libart.so",abi::arm64::android13::kToNative));
  candidate.runnable_=reinterpret_cast<Transition>(hooks.FindSymbol("libart.so",abi::arm64::android13::kToRunnable));
  candidate.suspend_=reinterpret_cast<decltype(candidate.suspend_)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendConstruct));
  candidate.resume_=reinterpret_cast<decltype(candidate.resume_)>(hooks.FindSymbol("libart.so",abi::arm64::android_common::kSuspendDestroy));
  void* decoder=hooks.FindSymbol("libart.so",runnable_jni::GlobalDecoder::SymbolName());
  if(!candidate.current_||!candidate.native_||!candidate.runnable_||!candidate.suspend_||!candidate.resume_||
      !runnable_jni::GlobalDecoder::Bind(runnable_jni::GlobalDecoder::Representation::kMatchedReleaseNoPoison,
          decoder,&candidate.checked_decode_))return Fail(error,"Missing complete modern transition/exclusive-root named capabilities");
  candidate.decode_=reinterpret_cast<Decode>(decoder);*output=candidate;
  if(error)error->clear();return true;
}
bool Service::State(void* self,uint32_t* output)const {
  size_t extent=0;
  if(!self||!output||!method_identity::ReadableMappingExtent(self,&extent)||extent<4)return false;
  const auto* flags=static_cast<const uint32_t*>(self);
  *output=__atomic_load_n(flags,__ATOMIC_RELAXED)>>24;return true;
}
bool Service::Enter(const runnable_jni::Context& scope,void* opaque) {
  auto& request=*static_cast<Request*>(opaque);const Service& service=*request.service;
  uint32_t state=0;
  if(scope.self!=request.self||service.current_()!=scope.self||!service.State(scope.self,&state)||state!=kRunnable)
    return Fail(request.error,"Actual Runnable callback does not match modern state source");
  for(size_t i=0;i<request.count;++i) {
    void* checked=nullptr;
    if(!service.checked_decode_.Decode(scope,request.globals[i],&checked)||!checked||
        reinterpret_cast<void*>(service.decode_(scope.self,request.globals[i]).reference)!=checked)
      return Fail(request.error,"Owned Runnable global identity did not match named decoder");
  }
  // Preallocate before releasing the shared mutator lock. Every pointer below
  // is reloaded after SuspendAll, never carried across its possible GC entry.
  std::vector<void*> roots(request.count);
  service.native_(scope.self);
  struct Restore {
    const Service& service;void* self;
    ~Restore(){service.runnable_(self);}
  } restore{service,scope.self};
  if(!service.State(scope.self,&state)||state!=kNative)
    return Fail(request.error,"Named pure transition did not establish Native state");
  abi::arm64::android_common::SuspendAll storage{};
  service.suspend_(&storage,"artdcevm owned exclusive roots",false);
  struct Release {
    const Service& service;void* storage;
    ~Release(){service.resume_(storage);}
  } release{service,&storage};
  for(size_t i=0;i<request.count;++i) {
    // No public JNI calls while Native owns the exclusive mutator lock.
    roots[i]=reinterpret_cast<void*>(service.decode_(scope.self,request.globals[i]).reference);
    if(!roots[i])return Fail(request.error,"Owned global is empty under exclusive mutator access");
  }
  return request.operation({scope.self,roots.data(),roots.size()},request.data);
}
bool Service::With(JNIEnv* env,const jobject* globals,size_t count,Operation operation,void* data,std::string* error)const {
  if(error)error->clear();
  if(!env||!globals||!count||count>128||!operation||!current_||env->ExceptionCheck())
    return Fail(error,"Invalid owned exclusive-root request");
  for(size_t i=0;i<count;++i)
    if(!globals[i]||env->GetObjectRefType(globals[i])!=JNIGlobalRefType||env->ExceptionCheck())
      return Fail(error,"Exclusive roots require independently owned strong globals");
  void* self=current_();uint32_t state=0;
  // This independent ordinary Native JNI boundary rejects legacy cookie-based
  // JNI transitions before calling a same-spelled, incompatible symbol.
  if(!State(self,&state)||state!=kNative)return Fail(error,"Native JNI boundary does not match modern state source");
  Request request{this,globals,count,operation,data,self,error};
  const auto result=runnable_executor::With(env,globals[0],Enter,&request);
  if(result!=runnable_jni::Result::kSuccess)
    return error&& !error->empty()?false:Fail(error,"Owned exclusive operation failed or threw; scopes restored");
  if(current_()!=self||!State(self,&state)||state!=kNative)
    return Fail(error,"Owned exclusive scope did not restore original Native JNI state");
  return true;
}
}
