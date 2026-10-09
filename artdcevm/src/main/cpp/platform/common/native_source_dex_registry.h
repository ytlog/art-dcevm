#pragma once
#include "platform/common/source_handle_roots.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/runtime_services.h"
namespace artdcevm::native_source_dex_registry {
// Named ClassLinker declarations, independently checked against an owned public
// Class's actual DexCache/DEX. Queries may lock/safepoint; never use them during
// a borrowed cache writer or final GC-disabled no-allocation window.
class Service final {
 public:
 static bool Bind(HookEngine&,const runtime_services::Snapshot&,Service*,std::string*);
 bool Calibrate(const runnable_jni::Context&,jobject strong_fixture,
                const modern_field_metadata::FieldMetadata&,source_handle_roots::Service&,std::string*);
 bool Calibrated()const noexcept{return calibrated_;}
 // Caller supplies the source-owned GC-updated cache slot. It is refreshed
 // after EACH native registry query; no borrowed managed pointer spans a query.
 bool Unregistered(const runnable_jni::Context&,source_handle_roots::Pool&,
                   const void* cache_root,const void* expected_dex,std::string*)const;
 private:
 using Object=abi::arm64::android11::ReleaseObjectPointer;
 using Registered=bool(*)(void*,void*,const void*);
 using Find=Object(*)(void*,void*,const void*);
 void*linker_=nullptr;Registered registered_=nullptr;Find find_=nullptr;
 bool calibrated_=false;
};
}
