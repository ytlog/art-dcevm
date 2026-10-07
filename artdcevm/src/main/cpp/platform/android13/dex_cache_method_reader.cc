#include "platform/android13/dex_cache_method_reader.h"
#include "platform/android13/art_method_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/validation_metadata_abi.h"
#include <cstring>
#include <vector>
#include <jvmti.h>
namespace artdcevm::android13 {

namespace {
std::atomic<bool> ready{false};
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool ReadCache(void* cache,uint32_t index,void** output) {
  using namespace dex_cache_method_abi;
  // AllocArray publishes the initialized array after its count with volatile
  // stores. Acquire the pointer before consulting the count or the pairs.
  auto* bytes=static_cast<const unsigned char*>(cache);
  const auto address=__atomic_load_n(reinterpret_cast<const uint64_t*>(bytes+offsetof(Cache,methods)),__ATOMIC_ACQUIRE);
  const auto count=__atomic_load_n(reinterpret_cast<const uint32_t*>(bytes+offsetof(Cache,num_methods)),__ATOMIC_RELAXED);
  return Lookup(reinterpret_cast<const std::atomic<Pair>*>(address),count,index,output);
}
struct Frame {
  JNIEnv* env;bool valid;
  explicit Frame(JNIEnv* e):env(e),valid(e->PushLocalFrame(8)==JNI_OK){}
  ~Frame(){if(valid)env->PopLocalFrame(nullptr);}
};
}
bool ReadCachedMethod(void* cache,uint32_t index,void** output) {
  if(output)*output=nullptr;
  return ready.load(std::memory_order_acquire) && cache && output && ReadCache(cache,index,output);
}
bool ConfigureDexCacheMethodReader(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,
                                  jobject fixture,std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"DexCache reader requires owned calibration");
  using Resolve=void*(*)(void*,void*,void*,void*,uint32_t);
  auto resolve=reinterpret_cast<Resolve>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker18FindResolvedMethodENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEj"));
  if(!resolve)return Fail(error,"Missing named DexCache calibration resolver");
  JavaVM* vm=nullptr;jvmtiEnv* ti=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK || vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)
    return Fail(error,"DexCache calibration requires JVMTI");
  Frame frame(env);if(!frame.valid)return Fail(error,"DexCache calibration local frame unavailable");
  jclass klass=env->GetObjectClass(fixture),cache_class=env->FindClass("java/lang/DexCache");
  if(!klass || !cache_class || env->ExceptionCheck())return Fail(error,"DexCache calibration classes unavailable");
  jint count=0;jmethodID* ids=nullptr;
  if(ti->GetClassMethods(klass,&count,&ids)!=JVMTI_ERROR_NONE || count<=0)
    return Fail(error,"DexCache calibration methods unavailable");
  std::vector<jmethodID> methods(ids,ids+count);ti->Deallocate(reinterpret_cast<unsigned char*>(ids));
  jfieldID* field_ids=nullptr;
  if(ti->GetClassFields(cache_class,&count,&field_ids)!=JVMTI_ERROR_NONE)return Fail(error,"DexCache field metadata unavailable");
  struct Field {jfieldID id;size_t offset;};std::vector<Field> fields;
  for(jint i=0;i<count;++i) {
    char* name=nullptr;char* signature=nullptr;
    if(ti->GetFieldName(cache_class,field_ids[i],&name,&signature,nullptr)==JVMTI_ERROR_NONE && name && signature) {
      using C=dex_cache_method_abi::Cache;
      if(std::strcmp(name,"dexFile")==0 && std::strcmp(signature,"J")==0)fields.push_back({field_ids[i],offsetof(C,dex_file)});
      if(std::strcmp(name,"resolvedMethods")==0 && std::strcmp(signature,"J")==0)fields.push_back({field_ids[i],offsetof(C,methods)});
      if(std::strcmp(name,"numResolvedMethods")==0 && std::strcmp(signature,"I")==0)fields.push_back({field_ids[i],offsetof(C,num_methods)});
    }
    if(name)ti->Deallocate(reinterpret_cast<unsigned char*>(name));
    if(signature)ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
  }
  if(field_ids)ti->Deallocate(reinterpret_cast<unsigned char*>(field_ids));
  if(fields.size()!=3)return Fail(error,"DexCache source fields differ from public metadata");
  NativeObjectAccess access(env);if(!access.Supported())return Fail(error,"DexCache native access unavailable");
  for(const auto& field:fields) {
    void* native=DecodeFieldId(field.id);
    if(!native || Read<uint32_t>(native,offsetof(field_copy_abi::ArtField,offset))!=field.offset)
      return Fail(error,"DexCache source offsets differ from declared fields");
  }
  HandleRootScope roots(access.Self(),3);
  roots.Set(0,access.Decode(klass));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,dex_cache)));
  roots.Set(2,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,class_loader)));
  if(!roots.Get(1))return Fail(error,"Owned fixture has no DexCache");
  for(auto id:methods) {
    void* method=DecodeMethodId(id);if(!method)return Fail(error,"Owned method decoder unavailable");
    uint32_t index=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index));
    if(index==UINT32_MAX)return Fail(error,"Owned method has no DEX index");
    // FindResolvedMethod writes the pair to DexCache. The similarly named
    // ResolveMethodWithoutInvokeType returns a method without filling a miss.
    if(resolve(RuntimeClassLinker(),roots.Get(0),roots.Get(1),roots.Get(2),index)!=method)
      return Fail(error,"Named resolver disagrees with public owned method identity");
    void* cached=nullptr;
    if(!ReadCache(roots.Get(1),index,&cached) || cached!=method)
      return Fail(error,"Atomic DexCache method entry differs from named resolution");
  }
  ready.store(true,std::memory_order_release);
  return true;
}
}
