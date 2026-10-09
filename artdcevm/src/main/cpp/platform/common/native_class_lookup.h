#pragma once
#include "api/hook_engine.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/runtime_services.h"
namespace artdcevm::native_class_lookup {
// Fixed FindClass declarations differ by an explicit descriptor hash argument.
// Symbol presence alone is not a lookup grant: an owned public Class and live
// loader Handle must independently roundtrip through the selected original.
class Service final {
 public:
 static bool Bind(HookEngine&,const runtime_services::Snapshot&,Service*,std::string*);
 bool Calibrate(const runnable_jni::Context&,jclass owned_class,
                const modern_field_metadata::FieldMetadata&,
                source_handle_roots::Service&,std::string*);
 bool Calibrated()const noexcept{return calibrated_;}
 abi::arm64::android11::ReleaseObjectPointer Find(const runnable_jni::Context&,const char* descriptor,
                                                 const void* loader_handle)const;
 private:
 using Obj=abi::arm64::android11::ReleaseObjectPointer;
 using Plain=Obj(*)(void*,void*,const char*,const void*);
 using Hashed=Obj(*)(void*,void*,const char*,size_t,const void*);
 using Hash=uint32_t(*)(const char*);
 void* linker_=nullptr;Plain plain_=nullptr;Hashed hashed_=nullptr;Hash hash_=nullptr;
 source_handle_roots::Service* roots_=nullptr;bool calibrated_=false;
};
}
