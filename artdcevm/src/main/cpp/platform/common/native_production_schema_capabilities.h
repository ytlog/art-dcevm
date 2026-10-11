#pragma once
#include "platform/common/native_hierarchy_session.h"
#include "platform/common/native_class_lookup.h"
#include "platform/common/native_source_dex_registry.h"

namespace artdcevm::native_production_schema_capabilities {
// Persistent production services, calibrated through ordinary original source
// operations. Hook installation and the first observed Handle are not readiness.
class Owner final {
 public:
 Owner()=default;
 ~Owner();
 Owner(const Owner&)=delete;Owner&operator=(const Owner&)=delete;
 bool Prepare(HookEngine&,const modern_field_storage::Service&,
              const modern_field_metadata::FieldMetadata&,
              const runtime_services::Snapshot&,const runnable_jni::Context& actual_native,
              std::string*);
 // Caller is in independently calibrated ordinary Native JNI. Each hook
 // installer enters and leaves its actual named ScopedSuspendAll exactly once.
 bool InstallHooks(native_hierarchy_source_checks::FailureObserver,std::string*);
 bool Begin(void* actual_driver,const runnable_jni::Context& actual_runnable,
            jobject owned_strong_original_class,std::string*);
 // Called in the original Run owner, after the original has returned. Drops
 // calibration scope before any JNI/registry/monitor calibration can safepoint.
 bool End(void* exact_driver,bool original_success,std::string*);
 bool Prepared()const noexcept{return prepared_;}
 bool HooksInstalled()const noexcept{return installed_;}
 bool Ready()const noexcept;
 const char* Diagnostic()const noexcept;
 source_handle_roots::Service& Roots()noexcept{return roots_;}
 const source_handle_roots::Service& Roots()const noexcept{return roots_;}
 native_hierarchy_allocator::Service& Allocator()noexcept{return allocator_;}
 const native_source_dex_registry::Service& DexRegistry()const noexcept{return registry_;}
 const native_class_lookup::Service& Lookup()const noexcept{return lookup_;}
 private:
 static abi::arm64::android11::ReleaseObjectPointer FindOriginal(void*,void*,const char*,void*);
 bool ContextMatches(const runnable_jni::Context&)const noexcept;
 HookEngine* hooks_=nullptr;
 const modern_field_storage::Service* storage_=nullptr;
 const modern_field_metadata::FieldMetadata* metadata_=nullptr;
 runtime_services::Snapshot runtime_{};
 source_handle_roots::Service roots_;
 native_hierarchy_allocator::Service allocator_;
 native_source_dex_registry::Service registry_;
 native_class_lookup::Service lookup_;
 native_hierarchy_source_checks::FailureObserver observer_=nullptr;
 std::unique_ptr<native_hierarchy_session::CalibrationScope> scope_;
 runnable_jni::Context context_{};
 runnable_jni::Context native_context_{};
 abi::arm64::android11::CurrentThread current_=nullptr;
 unsigned state_shift_=0;uint32_t native_state_=0;
 void* driver_=nullptr;jobject fixture_=nullptr;
 bool prepared_=false,installed_=false,completed_=false;
};
}
