#include "platform/android13/method_access.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/dex_cache_method_reader.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/loaded_class_guards.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/validation_metadata_abi.h"
#include <atomic>
#include <cstring>
#include <jvmti.h>
namespace artdcevm::android13 {
namespace {
using Lookup=void*(*)(void*,uint16_t,void*,void*);
using ClassError=void(*)(void*,void*,void*,uint32_t);
using MemberError=void(*)(void*,void*);
struct Functions {Lookup lookup;ClassError class_error;MemberError member_error;};
Functions functions{};
std::atomic<bool> ready{false};
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
MethodAccessResult Check(const Functions& bound,void* linker,void* cache_handle,uint32_t index,
    void* method,void* referrer,uint32_t invoke_type,std::string* error) {
  if(!referrer)return MethodAccessResult::Allowed;
  if(!method || !linker || !cache_handle) {
    Fail(error,"Resolved method access requires explicit context");return MethodAccessResult::Unavailable;
  }
  HandleRootScope roots(CurrentHierarchyThread(),3);
  roots.Set(0,ReadValidationMethodClass(referrer));
  roots.Set(1,ReadValidationMethodClass(method));
  if(!roots.Get(0) || !roots.Get(1)) {
    Fail(error,"Resolved method access has no declaring class");return MethodAccessResult::Unavailable;
  }
  if(!CanAccessValidationClass(roots.Get(0),roots.Get(1))) {
    void* cache=reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(cache_handle,0)));
    evolution::MappedDexMethodReference reference;
    if(!ReadCachedMethodReference(cache,index,&reference,error))return MethodAccessResult::Unavailable;
    // Class::ResolvedMethodAccessTest uses the referring class's loader here,
    // rather than replacing the explicit cache or using the method's loader.
    void* loader=ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,class_loader));
    roots.Set(2,bound.lookup(linker,reference.class_index,cache,loader));
    if(!roots.Get(2)) {
      Fail(error,"Resolved method access reference class unavailable");return MethodAccessResult::Unavailable;
    }
    if(!CanAccessValidationClass(roots.Get(0),roots.Get(2))) {
      bound.class_error(roots.Get(0),roots.Get(2),method,invoke_type);
      return MethodAccessResult::Exception;
    }
  }
  const auto flags=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,access_flags));
  if(CanAccessValidationMember(roots.Get(0),roots.Get(1),flags))return MethodAccessResult::Allowed;
  bound.member_error(roots.Get(0),method);
  return MethodAccessResult::Exception;
}
struct Frame {
  JNIEnv* env;bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(16)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
bool ExpectedError(JNIEnv* env,jclass type,jmethodID get_message,const char* kind,const char* member) {
  auto exception=env->ExceptionOccurred();
  if(!exception)return false;
  env->ExceptionClear();
  bool valid=env->IsInstanceOf(exception,type);
  auto message=valid?static_cast<jstring>(env->CallObjectMethod(exception,get_message)):nullptr;
  const char* text=message?env->GetStringUTFChars(message,nullptr):nullptr;
  valid=valid && text && std::strstr(text,kind) && std::strstr(text,member);
  if(text)env->ReleaseStringUTFChars(message,text);
  if(message)env->DeleteLocalRef(message);
  env->DeleteLocalRef(exception);
  return valid && !env->ExceptionCheck();
}
}
MethodAccessResult CheckResolvedMethodAccess(void* linker,void* cache_handle,uint32_t index,
    void* method,void* referrer,uint32_t invoke_type,std::string* error) {
  if(!ready.load(std::memory_order_acquire)) {
    Fail(error,"Resolved method access has not been calibrated");return MethodAccessResult::Unavailable;
  }
  return Check(functions,linker,cache_handle,index,method,referrer,invoke_type,error);
}
bool ConfigureMethodAccess(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,jobject fixture,
                           std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"Method access requires owned calibration");
  const Functions bound{
    reinterpret_cast<Lookup>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker18LookupResolvedTypeENS_3dex9TypeIndexENS_6ObjPtrINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE")),
    reinterpret_cast<ClassError>(hooks.FindSymbol("libart.so",
      "_ZN3art45ThrowIllegalAccessErrorClassForMethodDispatchENS_6ObjPtrINS_6mirror5ClassEEES3_PNS_9ArtMethodENS_10InvokeTypeE")),
    reinterpret_cast<MemberError>(hooks.FindSymbol("libart.so",
      "_ZN3art29ThrowIllegalAccessErrorMethodENS_6ObjPtrINS_6mirror5ClassEEEPNS_9ArtMethodE"))};
  if(!bound.lookup || !bound.class_error || !bound.member_error)return Fail(error,"Missing named method access operation");
  Frame frame(env);if(!frame.valid)return Fail(error,"Method access local frame unavailable");
  auto klass=env->GetObjectClass(fixture);
  auto peer=env->FindClass("com/github/ytlog/artdcevm/internal/CalibrationFixture");
  auto object=env->FindClass("java/lang/Object");
  auto error_class=env->FindClass("java/lang/IllegalAccessError");
  auto get_message=error_class?env->GetMethodID(error_class,"getMessage","()Ljava/lang/String;"):nullptr;
  auto compute=klass?env->GetMethodID(klass,"compute","()I"):nullptr;
  auto private_method=klass?env->GetMethodID(klass,"privateAccessCalibration","()I"):nullptr;
  auto peer_method=peer?env->GetMethodID(peer,"compute","()I"):nullptr;
  auto boot_method=object?env->GetMethodID(object,"hashCode","()I"):nullptr;
  if(!error_class || !get_message || !compute || !private_method || !peer_method || !boot_method || env->ExceptionCheck())
    return Fail(error,"Method access calibration members unavailable");
  JavaVM* vm=nullptr;jvmtiEnv* ti=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK || vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)
    return Fail(error,"Method access calibration requires JVMTI");
  jint private_flags=0;
  if(ti->GetMethodModifiers(private_method,&private_flags)!=JVMTI_ERROR_NONE || !(private_flags&2))
    return Fail(error,"Method access private fixture declaration unavailable");
  NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Method access native context unavailable");
  HandleRootScope roots(access.Self(),2);
  roots.Set(0,access.Decode(klass));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,dex_cache)));
  auto method=DecodeMethodId(compute),hidden=DecodeMethodId(private_method);
  auto referring=DecodeMethodId(peer_method),boot=DecodeMethodId(boot_method);
  if(!method || !hidden || !referring || !boot || !roots.Get(1))return Fail(error,"Method access fixture decoding unavailable");
  const auto method_index=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index));
  const auto hidden_index=Read<uint32_t>(hidden,offsetof(art_method_abi::ArtMethod,dex_method_index));
  constexpr uint32_t kVirtual=2;
  // Self/private and same-package/public access are allowed; null referrer skips checks.
  if(Check(bound,RuntimeClassLinker(),roots.Slot(1),hidden_index,hidden,hidden,1,error)!=MethodAccessResult::Allowed ||
     Check(bound,RuntimeClassLinker(),roots.Slot(1),method_index,method,referring,kVirtual,error)!=MethodAccessResult::Allowed ||
     Check(bound,RuntimeClassLinker(),roots.Slot(1),method_index,method,nullptr,kVirtual,error)!=MethodAccessResult::Allowed ||
     env->ExceptionCheck())return Fail(error,"Method access allowed calibration failed");
  if(Check(bound,RuntimeClassLinker(),roots.Slot(1),hidden_index,hidden,referring,1,error)!=MethodAccessResult::Exception ||
     !ExpectedError(env,error_class,get_message,"is inaccessible to class","privateAccessCalibration"))return Fail(error,"Method member access error calibration failed");
  if(Check(bound,RuntimeClassLinker(),roots.Slot(1),method_index,method,boot,kVirtual,error)!=MethodAccessResult::Exception ||
     !ExpectedError(env,error_class,get_message,"Illegal class access","compute"))return Fail(error,"Method class access error calibration failed");
  functions=bound;ready.store(true,std::memory_order_release);return true;
}
}
