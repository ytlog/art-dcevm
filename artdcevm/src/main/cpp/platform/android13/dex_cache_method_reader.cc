#include "platform/android13/dex_cache_method_reader.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/field_copy_metadata_abi.h"
#include "platform/android13/handle_root_scope.h"
#include "platform/android13/heap_reference.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/common/dex_file_metadata.h"
#include <cstring>
#include <vector>
#include <jvmti.h>
namespace artdcevm::android13 {

namespace {
std::atomic<bool> ready{false};
using LookupType=void*(*)(void*,uint16_t,void*,void*);
using ResolveType=void*(*)(void*,uint16_t,void*,void*);
struct TypeFunctions {LookupType lookup;ResolveType resolve;};
TypeFunctions type_functions{};
template<class T>T Read(const void* object,size_t offset) {
  T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(value));return value;
}
bool Fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool ReadCache(void* cache,uint32_t index,void** output) {
  return method_cache::ReadCache(33,cache,sizeof(method_cache::LegacyCache),0,index,output);
}
bool ReadMapping(void* cache,evolution::MappedDexMetadata* output,std::string* error) {
  return dex_file_metadata::ReadCache(33,cache,sizeof(method_cache::LegacyCache),
      sizeof(validation_abi::DexMappingPrefix),0,output,error);
}
bool ReadReference(void* cache,uint32_t index,evolution::MappedDexMethodReference* output,std::string* error) {
  evolution::MappedDexMetadata map{};
  return ReadMapping(cache,&map,error) && evolution::ReadMappedDexMethodReference(map,index,output,error);
}

void* HandleValue(void* slot) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(Read<uint32_t>(slot,0)));
}
MethodClassResolution ResolveClass(const TypeFunctions& functions,void* linker,void* cache_handle,
    void* loader_handle,uint16_t class_index,bool cached_method,HandleRootScope& output,size_t slot) {
  output.Set(slot,nullptr);
  void* klass=nullptr;
  if(cached_method)klass=functions.lookup(linker,class_index,HandleValue(cache_handle),HandleValue(loader_handle));
  if(!klass)klass=functions.resolve(linker,class_index,cache_handle,loader_handle);
  // Do not call a suspending operation between receiving the ObjPtr and rooting.
  output.Set(slot,klass);
  return klass?MethodClassResolution::Resolved:MethodClassResolution::Exception;
}
struct TiText {
  jvmtiEnv* ti;char* value=nullptr;
  ~TiText(){if(value)ti->Deallocate(reinterpret_cast<unsigned char*>(value));}
};
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
bool ReadCachedMethodReference(void* cache,uint32_t index,evolution::MappedDexMethodReference* output,
                               std::string* error) {
  if(!ready.load(std::memory_order_acquire) || !cache || !output || !error)
    return Fail(error,"DexCache method reference reader unavailable");
  return ReadReference(cache,index,output,error);
}
bool ReadCachedDexSupportsDefaultMethods(void* cache,bool* output,std::string* error) {
  if(!ready.load(std::memory_order_acquire) || !cache || !output || !error)
    return Fail(error,"DexCache default-method declaration reader unavailable");
  evolution::MappedDexMetadata map{};
  return ReadMapping(cache,&map,error) && evolution::ReadMappedDexSupportsDefaultMethods(map,output,error);
}
MethodClassResolution ResolveCachedMethodClass(void* linker,void* cache_handle,void* loader_handle,
    uint32_t method_index,bool cached_method,HandleRootScope& output,size_t slot,std::string* error) {
  output.Set(slot,nullptr);
  if(!ready.load(std::memory_order_acquire) || !linker || !cache_handle || !loader_handle || !error) {
    Fail(error,"Explicit-cache method class resolver unavailable");
    return MethodClassResolution::Unavailable;
  }
  void* cache=HandleValue(cache_handle);
  if(!cache) {
    Fail(error,"Explicit-cache method class resolver requires a cache");
    return MethodClassResolution::Unavailable;
  }
  evolution::MappedDexMethodReference reference;
  if(!ReadReference(cache,method_index,&reference,error))return MethodClassResolution::Unavailable;
  return ResolveClass(type_functions,linker,cache_handle,loader_handle,reference.class_index,
                      cached_method,output,slot);
}
bool ConfigureDexCacheMethodReader(HookEngine& hooks,const DeviceProfile& profile,JNIEnv* env,
                                  jobject fixture,std::string* error) {
  ready.store(false,std::memory_order_release);
  if(!profile.Matched() || !env || !fixture)return Fail(error,"DexCache reader requires owned calibration");
  using Resolve=void*(*)(void*,void*,void*,void*,uint32_t);
  auto resolve=reinterpret_cast<Resolve>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker18FindResolvedMethodENS_6ObjPtrINS_6mirror5ClassEEENS1_INS2_8DexCacheEEENS1_INS2_11ClassLoaderEEEj"));
  if(!resolve)return Fail(error,"Missing named DexCache calibration resolver");
  const TypeFunctions types{
    reinterpret_cast<LookupType>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker18LookupResolvedTypeENS_3dex9TypeIndexENS_6ObjPtrINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE")),
    reinterpret_cast<ResolveType>(hooks.FindSymbol("libart.so",
      "_ZN3art11ClassLinker11ResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE"))};
  if(!types.lookup || !types.resolve)return Fail(error,"Missing named explicit-cache class resolution");
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
  struct MethodMetadata {std::string name,signature;};
  std::vector<MethodMetadata> metadata;
  for(auto id:methods) {
    TiText name{ti},signature{ti};
    const auto status=ti->GetMethodName(id,&name.value,&signature.value,nullptr);
    const bool valid=status==JVMTI_ERROR_NONE && name.value && signature.value;
    if(valid)metadata.push_back({name.value,signature.value});
    if(!valid)return Fail(error,"Owned method metadata unavailable");
  }
  if(metadata.size()!=methods.size())return Fail(error,"Owned method metadata incomplete");
  TiText class_signature{ti};
  const auto class_status=ti->GetClassSignature(klass,&class_signature.value,nullptr);
  std::string owner=class_signature.value?class_signature.value:"";
  if(class_status!=JVMTI_ERROR_NONE || owner.empty())return Fail(error,"Owned class signature unavailable");
  jfieldID* field_ids=nullptr;
  if(ti->GetClassFields(cache_class,&count,&field_ids)!=JVMTI_ERROR_NONE)return Fail(error,"DexCache field metadata unavailable");
  struct Field {jfieldID id;size_t offset;};std::vector<Field> fields;
  for(jint i=0;i<count;++i) {
    char* name=nullptr;char* signature=nullptr;
    if(ti->GetFieldName(cache_class,field_ids[i],&name,&signature,nullptr)==JVMTI_ERROR_NONE && name && signature) {
      using C=method_cache::LegacyCache;
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
  HandleRootScope roots(access.Self(),4);
  roots.Set(0,access.Decode(klass));
  roots.Set(1,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,dex_cache)));
  roots.Set(2,ReadHeapReference(roots.Get(0),offsetof(validation_abi::ClassPrefix,class_loader)));
  if(!roots.Get(1))return Fail(error,"Owned fixture has no DexCache");
  for(size_t position=0;position<methods.size();++position) {
    auto id=methods[position];
    void* method=DecodeMethodId(id);if(!method)return Fail(error,"Owned method decoder unavailable");
    uint32_t index=Read<uint32_t>(method,offsetof(art_method_abi::ArtMethod,dex_method_index));
    if(index==UINT32_MAX)return Fail(error,"Owned method has no DEX index");
    evolution::MappedDexMethodReference reference;
    if(!ReadReference(roots.Get(1),index,&reference,error))return false;
    if(reference.declaring_class!=owner || reference.name!=metadata[position].name ||
       reference.descriptor!=metadata[position].signature)
      return Fail(error,"Explicit DexCache method reference differs from public metadata");
    for(bool cached_method:{false,true}) {
      if(ResolveClass(types,RuntimeClassLinker(),roots.Slot(1),roots.Slot(2),reference.class_index,
                      cached_method,roots,3)!=MethodClassResolution::Resolved || roots.Get(3)!=roots.Get(0))
        return Fail(error,"Explicit-cache class resolution differs from public owned class identity");
    }
    // FindResolvedMethod writes the pair to DexCache. The similarly named
    // ResolveMethodWithoutInvokeType returns a method without filling a miss.
    if(resolve(RuntimeClassLinker(),roots.Get(0),roots.Get(1),roots.Get(2),index)!=method)
      return Fail(error,"Named resolver disagrees with public owned method identity");
    void* cached=nullptr;
    if(!ReadCache(roots.Get(1),index,&cached) || cached!=method)
      return Fail(error,"Atomic DexCache method entry differs from named resolution");
  }
  type_functions=types;
  ready.store(true,std::memory_order_release);
  return true;
}
}
