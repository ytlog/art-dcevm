#include "platform/android13/hierarchy_allocation_abi.h"
#include "platform/android13/handle_root_scope_abi.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/class-inl.h"
#include "handle.h"
#include "dex/modifiers.h"
#include <cstring>
#include <type_traits>
namespace fixed=artdcevm::android13::hierarchy_allocation_abi;
using Cache=art::mirror::DexCache;
#define CHECK_OFFSET(accessor,field) static_assert(Cache::accessor().Uint32Value()==offsetof(fixed::Cache,field))
CHECK_OFFSET(ResolvedTypesOffset,types);
CHECK_OFFSET(ResolvedMethodsOffset,methods);
CHECK_OFFSET(ResolvedFieldsOffset,fields);
CHECK_OFFSET(NumResolvedTypesOffset,num_types);
CHECK_OFFSET(NumResolvedMethodsOffset,num_methods);
CHECK_OFFSET(NumResolvedFieldsOffset,num_fields);
#undef CHECK_OFFSET
static_assert(sizeof(art::mirror::TypeDexCachePair)==sizeof(fixed::TypePair));
static_assert(alignof(art::mirror::TypeDexCachePair)==alignof(fixed::TypePair));
static_assert(offsetof(art::mirror::TypeDexCachePair,object)==offsetof(fixed::TypePair,object));
static_assert(offsetof(art::mirror::TypeDexCachePair,index)==offsetof(fixed::TypePair,index));
static_assert(sizeof(art::mirror::MethodDexCachePair)==sizeof(fixed::NativePair));
static_assert(alignof(art::mirror::MethodDexCachePair)==alignof(fixed::NativePair));
static_assert(offsetof(art::mirror::MethodDexCachePair,object)==offsetof(fixed::NativePair,object));
static_assert(offsetof(art::mirror::MethodDexCachePair,index)==offsetof(fixed::NativePair,index));
static_assert(sizeof(art::mirror::FieldDexCachePair)==sizeof(fixed::NativePair));
static_assert(std::is_trivially_copyable_v<art::Handle<art::mirror::Class>>);
static_assert(sizeof(art::Handle<art::mirror::Class>)==sizeof(void*));
static_assert(std::is_trivially_copyable_v<art::ObjPtr<art::mirror::Class>>);
static_assert(sizeof(art::ObjPtr<art::mirror::Class>)==sizeof(void*));
static_assert(art::kAccClassIsProxy==0x40000);
static_assert(offsetof(art::dex::ClassDef,superclass_idx_)==8);
static_assert(sizeof(art::dex::TypeIndex)==2);
// ClassPrefix private offsets are statically calibrated in method_validation_guard.cc.
extern "C" bool ArtDcevmSourceProbeHierarchyCache(void* raw) {
  if(!raw || art::kIsDebugBuild || art::kObjPtrPoisoning || art::kPoisonHeapReferences) return false;
  auto* cache=static_cast<Cache*>(raw);
  fixed::Cache own;std::memcpy(&own,raw,sizeof(own));
  return own.types==reinterpret_cast<uintptr_t>(cache->GetResolvedTypes()) &&
      own.methods==reinterpret_cast<uintptr_t>(cache->GetResolvedMethods()) &&
      own.fields==reinterpret_cast<uintptr_t>(cache->GetResolvedFields()) &&
      own.num_types==cache->NumResolvedTypes() && own.num_methods==cache->NumResolvedMethods() &&
      own.num_fields==cache->NumResolvedFields();
}
