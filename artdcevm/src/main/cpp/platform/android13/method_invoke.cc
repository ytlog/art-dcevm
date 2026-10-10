#include "platform/android13/method_invoke.h"
#include "platform/android13/calibration_exception.h"
#include "platform/android13/dex_cache_method_reader.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/android13/heap_reference.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include <atomic>
#include <algorithm>
#include <cstring>
namespace artdcevm::android13 {
namespace {
using Check=bool(*)(void*,uint32_t);
using GetType=uint32_t(*)(void*);
using Throw=void(*)(uint32_t,uint32_t,void*,void*);
using ClassThrow=void(*)(void*,const char*,...);
struct Functions {Check check;GetType type;Throw error;ClassThrow class_error;};
Functions functions{};
std::atomic<bool> ready{false};
bool Valid(uint32_t type){return type<=5;}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
void* HandleValue(void* slot) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(slot,0)));
}
std::string PrettyOwner(const std::string& descriptor) {
  const size_t arrays=descriptor.find_first_not_of('[');
  std::string result;
  if(descriptor[arrays]=='L') {
    result=descriptor.substr(arrays+1,descriptor.size()-arrays-2);
    std::replace(result.begin(),result.end(),'/','.');
  } else {
    switch(descriptor[arrays]) {
      case 'Z':result="boolean";break;case 'B':result="byte";break;
      case 'C':result="char";break;case 'S':result="short";break;
      case 'I':result="int";break;case 'J':result="long";break;
      case 'F':result="float";break;case 'D':result="double";break;
    }
  }
  for(size_t i=0;i<arrays;++i)result+="[]";
  return result;
}
MethodInvokeResult CheckClass(const Functions& bound,void* class_handle,void* cache_handle,
    uint32_t index,uint32_t type,std::string* error) {
  if(!class_handle || !cache_handle || !Valid(type))return MethodInvokeResult::Unavailable;
  void* klass=HandleValue(class_handle),*cache=HandleValue(cache_handle);
  if(!klass || !cache)return MethodInvokeResult::Unavailable;
  const bool interface=(Read<uint32_t>(klass,offsetof(validation_abi::ClassPrefix,access_flags))&0x200)!=0;
  bool default_methods=false;
  if(type==1 && !ReadCachedDexSupportsDefaultMethods(cache,&default_methods,error))return MethodInvokeResult::Unavailable;
  const bool expect_interface=type==4;
  const bool expect_class=type==2 || (type==1 && !default_methods);
  if((!expect_interface || interface) && (!expect_class || !interface))return MethodInvokeResult::Compatible;
  evolution::MappedDexMethodReference reference;
  if(!ReadCachedMethodReference(cache,index,&reference,error))return MethodInvokeResult::Unavailable;
  const auto pretty=PrettyOwner(reference.declaring_class);
  bound.class_error(HandleValue(class_handle),expect_interface?
      "Found class %s, but interface was expected":"Found interface %s, but class was expected",pretty.c_str());
  return MethodInvokeResult::Exception;
}
struct Frame {
  JNIEnv* env;bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(16)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
}
MethodInvokeResult CheckMethodInvokeType(void* method,uint32_t type) {
  if(!ready.load(std::memory_order_acquire) || !method || !Valid(type))return MethodInvokeResult::Unavailable;
  return functions.check(method,type)?MethodInvokeResult::Mismatch:MethodInvokeResult::Compatible;
}
MethodInvokeResult CheckClassInvokeType(void* class_handle,void* cache_handle,uint32_t index,
    uint32_t type,std::string* error) {
  if(!ready.load(std::memory_order_acquire))return MethodInvokeResult::Unavailable;
  return CheckClass(functions,class_handle,cache_handle,index,type,error);
}
MethodInvokeResult ThrowMethodInvokeMismatch(void* method,void* referrer,uint32_t type) {
  if(!ready.load(std::memory_order_acquire) || !method || !Valid(type))return MethodInvokeResult::Unavailable;
  functions.error(type,functions.type(method),method,referrer);
  return MethodInvokeResult::Exception;
}
bool ConfigureMethodInvoke(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,jobject fixture,
                           std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"Method invoke requires owned calibration");
  const Functions bound{
    reinterpret_cast<Check>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod28CheckIncompatibleClassChangeENS_10InvokeTypeE")),
    reinterpret_cast<GetType>(hooks.FindSymbol("libart.so","_ZN3art9ArtMethod13GetInvokeTypeEv")),
    reinterpret_cast<Throw>(hooks.FindSymbol("libart.so","_ZN3art33ThrowIncompatibleClassChangeErrorENS_10InvokeTypeES0_PNS_9ArtMethodES2_")),
    reinterpret_cast<ClassThrow>(hooks.FindSymbol("libart.so","_ZN3art33ThrowIncompatibleClassChangeErrorENS_6ObjPtrINS_6mirror5ClassEEEPKcz"))};
  if(!bound.check || !bound.type || !bound.error || !bound.class_error)return Fail(error,"Missing named method invocation operation");
  Frame frame(env);if(!frame.valid)return Fail(error,"Method invoke local frame unavailable");
  auto klass=env->GetObjectClass(fixture);
  auto contract=env->FindClass("com/github/ytlog/artdcevm/internal/EvolutionCalibration$InvocationContract");
  auto object=env->FindClass("java/lang/Object");
  auto handle=env->FindClass("java/lang/invoke/MethodHandle");
  auto exception=env->FindClass("java/lang/IncompatibleClassChangeError");
  auto message=exception?env->GetMethodID(exception,"getMessage","()Ljava/lang/String;"):nullptr;
  // Fixed libdexfile/dex/invoke_type.h order: static, direct, virtual, super,
  // interface, polymorphic. Expectations follow ArtMethod::CheckIncompatibleClassChange.
  struct Case {jmethodID id;uint32_t actual;bool compatible[6];};
  Case cases[]={
    {klass?env->GetMethodID(klass,"compute","()I"):nullptr,2,{false,false,true,true,false,false}},
    {klass?env->GetMethodID(klass,"privateAccessCalibration","()I"):nullptr,1,{false,true,false,true,false,false}},
    {klass?env->GetMethodID(klass,"<init>","()V"):nullptr,1,{false,true,false,false,false,false}},
    {klass?env->GetStaticMethodID(klass,"staticInvokeCalibration","()I"):nullptr,0,{true,false,false,false,false,false}},
    {contract?env->GetMethodID(contract,"invoke","()I"):nullptr,4,{false,false,false,true,true,false}},
    {object?env->GetMethodID(object,"hashCode","()I"):nullptr,2,{false,false,true,true,true,false}},
    {handle?env->GetMethodID(handle,"invokeExact","([Ljava/lang/Object;)Ljava/lang/Object;"):nullptr,5,{false,false,true,true,false,true}}
  };
  if(!message || env->ExceptionCheck())return Fail(error,"Method invoke calibration classes unavailable");
  for(const auto& item:cases)if(!item.id)return Fail(error,"Method invoke calibration method unavailable");
  NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Method invoke native context unavailable");
  for(const auto& item:cases) {
    auto method=DecodeMethodId(item.id);
    if(!method || bound.type(method)!=item.actual)return Fail(error,"Named method invocation type disagrees with public declaration");
    for(uint32_t type=0;type<6;++type)
      if(bound.check(method,type)==item.compatible[type])return Fail(error,"Named invocation mismatch check disagrees with declared method category");
  }
  HandleRootScope roots(access.Self(),4);
  roots.Set(0,access.Decode(klass));roots.Set(2,access.Decode(contract));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,dex_cache)));
  roots.Set(3,ReadHeapReference(roots.Get(2),offsetof(validation_abi::ClassPrefix,dex_cache)));
  if(!roots.Get(1) || !roots.Get(3))return Fail(error,"Invocation class fixtures have no DexCache");
  const auto method_index=Read<uint32_t>(DecodeMethodId(cases[0].id),offsetof(art_method_abi::ArtMethod,dex_method_index));
  const auto interface_index=Read<uint32_t>(DecodeMethodId(cases[4].id),offsetof(art_method_abi::ArtMethod,dex_method_index));
  // Independently compare the live header declaration with its named libdexfile override.
  bool default_methods=false;
  if(!ReadCachedDexSupportsDefaultMethods(roots.Get(3),&default_methods,error))return false;
  auto dex=reinterpret_cast<void*>(Read<uint64_t>(roots.Get(3),offsetof(method_cache::LegacyCache,dex_file)));
  const auto map=Read<validation_abi::DexMappingPrefix>(dex,0);
  using Supports=bool(*)(void*);
  auto supports=reinterpret_cast<Supports>(hooks.FindSymbol("libdexfile.so",
      std::memcmp(map.begin,"cdex001\0",8)==0?
      "_ZNK3art14CompactDexFile22SupportsDefaultMethodsEv":"_ZNK3art15StandardDexFile22SupportsDefaultMethodsEv"));
  if(!supports || supports(dex)!=default_methods)return Fail(error,"Live DEX default-method declaration differs from named override");
  for(uint32_t type=0;type<6;++type) {
    auto actual=CheckClass(bound,roots.Slot(0),roots.Slot(1),method_index,type,error);
    if(type==4) {
      if(actual!=MethodInvokeResult::Exception ||
         !ConsumeCalibrationException(env,exception,message,{"Found class","interface was expected","EvolutionCalibration"}))
        return Fail(error,"Invoke-interface class mismatch calibration failed");
    } else if(actual!=MethodInvokeResult::Compatible || env->ExceptionCheck())
      return Fail(error,"Non-interface class invocation calibration failed");
    actual=CheckClass(bound,roots.Slot(2),roots.Slot(3),interface_index,type,error);
    if(type==2 || (type==1 && !default_methods)) {
      if(actual!=MethodInvokeResult::Exception ||
         !ConsumeCalibrationException(env,exception,message,{"Found interface","class was expected","InvocationContract"}))
        return Fail(error,"Interface class mismatch calibration failed");
    } else if(actual!=MethodInvokeResult::Compatible || env->ExceptionCheck())
      return Fail(error,"Interface class invocation calibration failed");
  }
  auto virtual_method=DecodeMethodId(cases[0].id),static_method=DecodeMethodId(cases[3].id);
  if(env->ExceptionCheck())return Fail(error,"Invocation predicate unexpectedly installed an exception");
  bound.error(0,bound.type(virtual_method),virtual_method,nullptr);
  if(!ConsumeCalibrationException(env,exception,message,{"compute","expected to be of type static","found to be of type virtual"}))
    return Fail(error,"Method invocation mismatch error calibration failed");
  bound.error(2,bound.type(static_method),static_method,virtual_method);
  if(!ConsumeCalibrationException(env,exception,message,{"staticInvokeCalibration","expected to be of type virtual","found to be of type static"}))
    return Fail(error,"Method invocation referrer error calibration failed");
  functions=bound;ready.store(true,std::memory_order_release);return true;
}
}
