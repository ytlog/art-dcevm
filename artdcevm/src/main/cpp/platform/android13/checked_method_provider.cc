#include "platform/android13/checked_method_provider.h"
#include "evolution/checked_method_resolution.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/calibration_exception.h"
#include "platform/android13/dex_cache_method_reader.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/method_access.h"
#include "platform/android13/method_invoke.h"
#include "platform/android13/method_lookup.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/retired_member_guard.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/symbols/android13.h"
#include <atomic>
#include <cstring>
#include <new>
namespace artdcevm::android13 {
namespace {
using Context=evolution::MethodResolutionContext;
std::atomic<bool> ready{false};
ThrowGuardException throw_error=nullptr;
struct Unavailable {};
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
class Provider final:public evolution::CheckedMethodResolution {
 public:
  Provider(std::string* error,bool read_cache):error_(error),read_cache_(read_cache),roots_(CurrentHierarchyThread(),1){}
  void* CachedMethod(const Context& c)override {
    void* result=nullptr;
    auto cache=reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(c.dex_cache_handle,0)));
    Require(ReadCachedMethod(cache,c.method_index,&result));
    cached_=read_cache_ && result!=nullptr;
    return cached_?result:nullptr;
  }
  bool ResolveAndRootClass(const Context& c)override {
    auto result=ResolveCachedMethodClass(c.linker,c.dex_cache_handle,c.class_loader_handle,
                                        c.method_index,cached_,roots_,0,error_);
    Require(result!=MethodClassResolution::Unavailable);
    return result==MethodClassResolution::Resolved;
  }
  bool RejectClassInvokeType(const Context& c)override {
    auto result=CheckClassInvokeType(roots_.Slot(0),c.dex_cache_handle,c.method_index,c.invoke_type,error_);
    Require(result!=MethodInvokeResult::Unavailable);return result==MethodInvokeResult::Exception;
  }
  void* FindResolvedMethod(const Context& c)override{return Find(c,false);}
  bool RejectAccess(const Context& c,void* method)override {
    auto result=CheckResolvedMethodAccess(c.linker,c.dex_cache_handle,c.method_index,method,c.referrer,c.invoke_type,error_);
    Require(result!=MethodAccessResult::Unavailable);return result==MethodAccessResult::Exception;
  }
  bool RejectMethodInvokeType(const Context& c,void* method)override {
    auto result=CheckMethodInvokeType(method,c.invoke_type);
    Require(result!=MethodInvokeResult::Unavailable);return result==MethodInvokeResult::Mismatch;
  }
  void* FindIncompatibleMethod(const Context& c)override{return Find(c,true);}
  void ThrowInvokeMismatch(const Context& c,void* method)override {
    Require(ThrowMethodInvokeMismatch(method,c.referrer,c.invoke_type)==MethodInvokeResult::Exception);
  }
  void ThrowMissingMethod(const Context& c)override {
    Require(android13::ThrowMissingMethod(roots_.Slot(0),c.dex_cache_handle,c.method_index,c.invoke_type,error_));
  }
 private:
  void Require(bool valid){if(!valid)throw Unavailable{};}
  void* Find(const Context& c,bool incompatible) {
    void* result=nullptr;
    Require(FindCheckedMethod(c.linker,roots_.Slot(0),c.dex_cache_handle,c.class_loader_handle,c.method_index,incompatible,&result));
    return result;
  }
  std::string* error_;bool read_cache_,cached_=false;HandleRootScope roots_;
};
void* Resolve(const Context& context,ThrowGuardException fail,bool read_cache=true) {
  std::string error;
  try {
    Provider source(&error,read_cache);
    return evolution::ResolveCheckedMethod(context,source);
  } catch(const std::bad_alloc&) {
    fail(CurrentHierarchyThread(),"Ljava/lang/OutOfMemoryError;","Checked method resolution allocation failed");
  } catch(const Unavailable&) {
    fail(CurrentHierarchyThread(),"Ljava/lang/LinkageError;",error.empty()?"Checked method resolution primitive unavailable":error.c_str());
  }
  return nullptr;
}
struct Frame {
  JNIEnv* env;bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(12)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
}
void* SourceResolveCheckedHandles(void* linker,uint32_t index,void* cache,void* loader,void* referrer,uint32_t type) {
  if(!ready.load(std::memory_order_acquire))return nullptr; // never published to an invocation hook before ready
  return Resolve({linker,index,cache,loader,referrer,type},throw_error);
}
CheckedHandleResolver SelectCheckedHandleResolver(HookEngine& hooks,bool force_source,std::string* error) {
  if(!ready.load(std::memory_order_acquire)) {
    Fail(error,"Checked Handle provider has not been calibrated");return nullptr;
  }
  if(!force_source)if(auto native=hooks.FindSymbol("libart.so",cas::kCheckedResolveHandles))
    return reinterpret_cast<CheckedHandleResolver>(native);
  return SourceResolveCheckedHandles;
}
bool ConfigureCheckedMethodProvider(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,
                                    jobject fixture,std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"Checked provider requires owned calibration");
  auto fail=reinterpret_cast<ThrowGuardException>(hooks.FindSymbol("libart.so",kGuardThrowNewException));
  using Native=void*(*)(void*,uint32_t,void*,void*,void*,uint32_t);
  auto native=reinterpret_cast<Native>(hooks.FindSymbol("libart.so",cas::kCheckedResolveHandles));
  if(!fail)return Fail(error,"Checked provider requires named boundary exception installer");
  Frame frame(env);if(!frame.valid)return Fail(error,"Checked provider local frame unavailable");
  auto klass=env->GetObjectClass(fixture);
  auto icce=env->FindClass("java/lang/IncompatibleClassChangeError");
  auto iae=env->FindClass("java/lang/IllegalAccessError");
  auto object=env->FindClass("java/lang/Object");
  auto linkage=env->FindClass("java/lang/LinkageError");
  auto message=icce?env->GetMethodID(icce,"getMessage","()Ljava/lang/String;"):nullptr;
  auto id=klass?env->GetMethodID(klass,"compute","()I"):nullptr;
  auto boot_id=object?env->GetMethodID(object,"hashCode","()I"):nullptr;
  if(!message || !iae || !linkage || !id || !boot_id || env->ExceptionCheck())return Fail(error,"Checked provider fixture unavailable");
  NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Checked provider native context unavailable");
  HandleRootScope roots(access.Self(),3);
  roots.Set(0,access.Decode(klass));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,dex_cache)));
  roots.Set(2,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,class_loader)));
  auto method=DecodeMethodId(id),boot=DecodeMethodId(boot_id);
  if(!method || !boot || !roots.Get(1))return Fail(error,"Checked provider fixture decoding unavailable");
  const auto index=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index));
  for(bool read_cache:{true,false}) {
    for(void* referrer:{static_cast<void*>(nullptr),method}) {
      Context c{RuntimeClassLinker(),index,roots.Slot(1),roots.Slot(2),referrer,2};
      if(Resolve(c,fail,read_cache)!=method || env->ExceptionCheck())return Fail(error,"Checked source provider differs from public owned method");
      if(native && (native(c.linker,index,c.dex_cache_handle,c.class_loader_handle,referrer,2)!=method || env->ExceptionCheck()))
        return Fail(error,"Named checked provider differs from owned method");
    }
    Context c{RuntimeClassLinker(),index,roots.Slot(1),roots.Slot(2),nullptr,0};
    if(Resolve(c,fail,read_cache)!=nullptr ||
       !ConsumeCalibrationException(env,icce,message,{"compute","expected to be of type static","found to be of type virtual"}))
      return Fail(error,"Checked source provider invocation error failed");
    c.referrer=boot;
    if(Resolve(c,fail,read_cache)!=nullptr ||
       !ConsumeCalibrationException(env,iae,message,{"Illegal class access","compute"}))
      return Fail(error,"Checked source provider access-before-invoke priority failed");
    if(native && (native(c.linker,index,c.dex_cache_handle,c.class_loader_handle,boot,0)!=nullptr ||
       !ConsumeCalibrationException(env,iae,message,{"Illegal class access","compute"})))
      return Fail(error,"Named provider access-before-invoke priority differs");
    c.invoke_type=4;
    if(Resolve(c,fail,read_cache)!=nullptr ||
       !ConsumeCalibrationException(env,icce,message,{"Found class","interface was expected"}))
      return Fail(error,"Checked source provider class-before-access priority failed");
  }
  // Preserve the calibrated owned method's valid cache hash slot; the huge ID
  // is invalid in DEX, so the negative probe reaches metadata validation.
  constexpr uint32_t mask=dex_cache_method_abi::kSlots-1;
  const uint32_t invalid_index=(UINT32_MAX & ~mask) | (index & mask);
  Context unavailable{RuntimeClassLinker(),invalid_index,roots.Slot(1),roots.Slot(2),nullptr,2};
  if(Resolve(unavailable,fail)!=nullptr ||
     !ConsumeCalibrationException(env,linkage,message,{"Invalid DEX table index"}))
    return Fail(error,"Checked provider unavailable-metadata boundary calibration failed");
  throw_error=fail;ready.store(true,std::memory_order_release);return true;
}
}
