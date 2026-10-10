#pragma once
#include "platform/common/frame_family_calibration.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/compiled_frame_dex_oracle.h"
#include "platform/common/native_nterp_frame_proof.h"
#include <jvmti.h>
namespace artdcevm::compiled_frame_fixture {
// Destroy in ordinary attached JNI after the source stop has ended. The worker
// is released before any fixture globals disappear; registered source roots
// remain owned by their same-thread Pool and actual stop owner.
class Owner final {
 public:
 static std::unique_ptr<Owner> Prepare(HookEngine&,JNIEnv*,jvmtiEnv*,jobjectArray original_installed_dex,std::string*);
 // Experimental cold invocation; no Nterp/compiled readiness is inferred.
 static std::unique_ptr<Owner> PrepareCold(HookEngine&,JNIEnv*,jvmtiEnv*,jobjectArray original_installed_dex,std::string*);
 ~Owner();
 Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
 bool RegisterRoots(const runnable_jni::Context&,source_handle_roots::Pool&,std::string*);
 bool Start(JNIEnv*,std::string*);
 bool CalibrateStopped(const native_source_stop::Session&,void* self,void* jit_cache,
      const source_handle_roots::StoppedReader&,
      active_frame_recovery_proof::Service::Readable,
      frame_family_calibration::Result*)const;
 bool CalibrateNterpStopped(const native_source_stop::Session&,void* self,
      const source_handle_roots::StoppedReader&,const modern_field_metadata::ReadContext&,
      native_nterp_frame_proof::Report*)const;
 bool Close(JNIEnv*,std::string*);
 private:
 Owner()=default;
 static std::unique_ptr<Owner> PrepareMode(HookEngine&,JNIEnv*,jvmtiEnv*,jobjectArray,bool warm,std::string*);
 static bool Resolve(const runnable_jni::Context&,void*);
 JavaVM* vm_=nullptr;jobject run_=nullptr;jclass fixture_=nullptr;
 jmethodID start_=nullptr,close_=nullptr;
 std::array<jobject,5> reflected_{};std::array<jobject,3> references_{};
 std::array<method_identity::Oracle,5> public_{};
 std::array<frame_family_calibration::MethodOracle,5> native_{};
 std::array<compiled_frame_dex_oracle::Frame,3> code_{};
 std::array<int32_t,3> integers_{};
 std::array<void*,3> slots_{};
 abi::common::FromReflectedMethod leaf_=nullptr;
 frame_family_calibration::Service service_;
 mutable frame_family_calibration::Service::Prepared prepared_;
 mutable std::array<decoded_frame_recovery::Location,16> scratch_{};
 std::array<native_nterp_frame_proof::Service,6> nterp_;
 std::array<bool,6> nterp_bound_{};
 void* root_owner_=nullptr;void* target_=nullptr;bool started_=false;
};
}
