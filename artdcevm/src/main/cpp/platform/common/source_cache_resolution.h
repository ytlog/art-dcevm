#pragma once
#include "api/hook_engine.h"
#include "platform/abi/arm64/common/cache_resolution.h"
namespace artdcevm::source_cache_resolution {
struct Witness {
 const void* cache_slot=nullptr;const void* class_slot=nullptr;const void* loader_slot=nullptr;
 uint32_t type_index=0,field_index=0,method_index=0;
 void* field=nullptr;void* method=nullptr;bool field_static=false;
};
// Snapshot must re-read the SAME rooted public Class/cache/loader after every
// potentially suspending resolve, validate exact Class->DexCache/loader and
// actual Runnable owner, and reject any pending exception. Slots stay live.
struct Objects {void* klass=nullptr;void* cache=nullptr;void* loader=nullptr;};
class Roots {
 public:virtual ~Roots()=default;
 virtual bool Snapshot(const Witness&,Objects* current,std::string*)const=0;
 // Evidence only after Snapshot succeeds; implementations must prove native
 // array membership before reading a foreign original return as ArtField.
 virtual std::string DescribeField(const Witness&,void*)const{return {}; }
};
class NativeWindow {
 public:virtual ~NativeWindow()=default;
 // Must independently prove ordinary Native JNI and the exact current
 // Thread/environment. This is a suspension precondition, not capability.
 virtual bool OrdinaryNative()const noexcept=0;
};
class Service final {
 public:
 static bool Bind(HookEngine&,void* observed_linker,Service*,std::string*);
 bool Resolve(const Witness&,const Roots&,std::string*)const;
 // Diagnostic only. A real ordinary-JNI Java call runs between suspended
 // installation/removal of unique source-typed observers. Never grants readiness.
 bool TraceJavaWarm(HookEngine&,const NativeWindow&,uint32_t field_index,void(*invoke)(void*),void*,std::string*);
 private:
 std::string java_trace_;
 void* linker_=nullptr;
 abi::arm64::android_common::ResolveCacheType type_=nullptr;
 abi::arm64::android_common::ResolveCacheMember field_=nullptr,method_=nullptr;
 abi::arm64::android_common::FindCacheField field_specific_=nullptr;
};
}
