#include "platform/android13/method_lookup.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/android13/calibration_exception.h"
#include "platform/android13/dex_cache_method_reader.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include <atomic>
#include <cstring>
namespace artdcevm::android13 {
namespace {
// Fixed Signature and libc++ basic_string_view declarations. No STL namespace
// is crossed; these trivial values use their source-declared pointer/size layout.
struct StringView {const char* data;size_t size;};
struct Signature {const void* dex_file;const void* proto_id;};
static_assert(sizeof(StringView)==16 && offsetof(StringView,size)==8 && sizeof(Signature)==16);
using Find=void*(*)(void*,void*,void*,void*,uint32_t);
using Missing=void(*)(uint32_t,void*,StringView,const Signature&);
struct Functions {Find resolved,incompatible;Missing missing;};
Functions functions{};
std::atomic<bool> ready{false};
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
void* HandleValue(void* slot) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(slot,0)));
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool MissingMethod(const Functions& bound,void* class_handle,void* cache_handle,uint32_t index,
                   uint32_t type,std::string* error) {
  if(!class_handle || !cache_handle || type>5)return Fail(error,"Missing-method error requires explicit rooted context");
  auto klass=HandleValue(class_handle),cache=HandleValue(cache_handle);
  if(!klass || !cache)return Fail(error,"Missing-method error has no class/cache");
  evolution::MappedDexMethodReference reference;
  if(!ReadCachedMethodReference(cache,index,&reference,error))return false;
  const Signature signature{reinterpret_cast<const void*>(Read<uint64_t>(cache,offsetof(method_cache::LegacyCache,dex_file))),reference.proto_id};
  const StringView name{reference.name.data(),reference.name.size()};
  bound.missing(type,HandleValue(class_handle),name,signature);
  return true;
}
struct Frame {
  JNIEnv* env;bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(12)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
}
bool FindCheckedMethod(void* linker,void* class_handle,void* cache_handle,void* loader_handle,
                       uint32_t index,bool incompatible,void** output) {
  if(output)*output=nullptr;
  if(!ready.load(std::memory_order_acquire) || !linker || !class_handle || !cache_handle || !loader_handle || !output)return false;
  auto klass=HandleValue(class_handle),cache=HandleValue(cache_handle);
  if(!klass || !cache)return false;
  *output=(incompatible?functions.incompatible:functions.resolved)(linker,klass,cache,HandleValue(loader_handle),index);
  return true;
}
bool ThrowMissingMethod(void* class_handle,void* cache_handle,uint32_t index,uint32_t type,std::string* error) {
  if(!ready.load(std::memory_order_acquire))return Fail(error,"Missing-method error has not been calibrated");
  return MissingMethod(functions,class_handle,cache_handle,index,type,error);
}
bool ConfigureMethodLookup(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,jobject fixture,
                           std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"Method lookup requires owned calibration");
  const Functions bound{
    reinterpret_cast<Find>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker18FindResolvedMethodENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEj")),
    reinterpret_cast<Find>(hooks.FindSymbol("libart.so","_ZN3art11ClassLinker22FindIncompatibleMethodENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEj")),
    reinterpret_cast<Missing>(hooks.FindSymbol("libart.so","_ZN3art22ThrowNoSuchMethodErrorENS_10InvokeTypeENS_6ObjPtrINS_6mirror5ClassEEENSt3__117basic_string_viewIcNS5_11char_traitsIcEEEERKNS_9SignatureE"))};
  if(!bound.resolved || !bound.incompatible || !bound.missing)return Fail(error,"Missing named method lookup/error operation");
  Frame frame(env);if(!frame.valid)return Fail(error,"Method lookup local frame unavailable");
  auto klass=env->GetObjectClass(fixture);
  auto contract=env->FindClass("com/github/ytlog/artdcevm/internal/EvolutionCalibration$InvocationContract");
  auto exception=env->FindClass("java/lang/NoSuchMethodError");
  auto message=exception?env->GetMethodID(exception,"getMessage","()Ljava/lang/String;"):nullptr;
  auto method_id=klass?env->GetMethodID(klass,"compute","()I"):nullptr;
  auto signature_id=klass?env->GetMethodID(klass,"signatureLookupCalibration","([Ljava/lang/String;I)J"):nullptr;
  auto interface_id=contract?env->GetMethodID(contract,"invoke","()I"):nullptr;
  if(!message || !method_id || !signature_id || !interface_id || env->ExceptionCheck())return Fail(error,"Method lookup calibration members unavailable");
  NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"Method lookup native context unavailable");
  HandleRootScope roots(access.Self(),6);
  roots.Set(0,access.Decode(klass));roots.Set(3,access.Decode(contract));
  for(size_t base:{size_t{0},size_t{3}}) {
    roots.Set(base+1,ReadHeapReference(roots.Get(base),offsetof(validation_abi::ClassPrefix,dex_cache)));
    roots.Set(base+2,ReadHeapReference(roots.Get(base),offsetof(validation_abi::ClassPrefix,class_loader)));
  }
  auto method=DecodeMethodId(method_id),interface_method=DecodeMethodId(interface_id);
  if(!method || !interface_method || !roots.Get(1) || !roots.Get(4))return Fail(error,"Method lookup fixture decoding unavailable");
  const auto index=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index));
  const auto interface_index=Read<uint32_t>(interface_method,offsetof(art_method_abi::ArtMethod,dex_method_index));
  if(bound.resolved(RuntimeClassLinker(),roots.Get(0),roots.Get(1),roots.Get(2),index)!=method ||
     bound.resolved(RuntimeClassLinker(),roots.Get(3),roots.Get(4),roots.Get(5),interface_index)!=interface_method ||
     bound.incompatible(RuntimeClassLinker(),roots.Get(0),roots.Get(1),roots.Get(2),index)!=nullptr ||
     bound.incompatible(RuntimeClassLinker(),roots.Get(3),roots.Get(4),roots.Get(5),interface_index)!=interface_method ||
     env->ExceptionCheck())return Fail(error,"Named method lookup differs from public owned identity");
  // The exception formatter accepts an existing method ID too. This validates
  // name/string_view/prototype ABI, not a real missing-method resolution yet.
  if(!MissingMethod(bound,roots.Slot(0),roots.Slot(1),index,2,error) ||
     !ConsumeCalibrationException(env,exception,message,{"No virtual method compute()I","EvolutionCalibration","or its super classes"}))
    return Fail(error,"Named missing-method error ABI calibration failed");
  if(!MissingMethod(bound,roots.Slot(3),roots.Slot(4),interface_index,4,error) ||
     !ConsumeCalibrationException(env,exception,message,{"No interface method invoke()I","InvocationContract","or its super classes"}))
    return Fail(error,"Named interface missing-method error ABI calibration failed");
  auto signature_method=DecodeMethodId(signature_id);
  if(!signature_method)return Fail(error,"Owned signature calibration decoder unavailable");
  const auto signature_index=Read<uint32_t>(signature_method,offsetof(art_method_abi::ArtMethod,dex_method_index));
  if(!MissingMethod(bound,roots.Slot(0),roots.Slot(1),signature_index,2,error) ||
     !ConsumeCalibrationException(env,exception,message,{"signatureLookupCalibration([Ljava/lang/String;I)J","EvolutionCalibration"}))
    return Fail(error,"Named parameter-list signature ABI calibration failed");
  functions=bound;ready.store(true,std::memory_order_release);return true;
}
}
