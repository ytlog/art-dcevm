#pragma once
#include "platform/common/native_final_write_listener.h"
#include "platform/common/native_schema_guard_verifier.h"
#include "platform/common/modern_field_guards.h"
namespace artdcevm::final_field_calibration {struct Bindings;bool Run(JNIEnv*,const Bindings&,std::string*);}
namespace artdcevm::final_field_service {
struct Bindings {
 native_schema_guard_verifier::Bindings native;
 retirement::RetiredFieldRegistry* registry=nullptr;
 // Independently calibrated native weak-reference decoder. A cleared weak
 // yields success+null. Borrowed values stay under the current mutator scope.
 bool (*decode_weak)(void*,void* actual_thread,jweak,void**)=nullptr;
 bool (*register_strong)(void*,JNIEnv*,jobject,std::string*)=nullptr;
 void (*forget_strong)(void*,jobject)=nullptr;
 // Public-JNI registration ties each owned jweak to its calibrated decoder.
 bool (*register_weak)(void*,JNIEnv*,jweak,std::string*)=nullptr;
 void (*forget_weak)(void*,jweak)=nullptr;
 void (*raise)(void*,void*,const char*,const char*)=nullptr;
};
// This process owner outlives its registered listener. JNI staging/cleanup and
// native final verification run under the original serialized transaction gate.
class Service final:public native_schema_guard_verifier::FinalFields,public native_schema_guard_verifier::AccessPermissions {
 public:
 explicit Service(Bindings);~Service();
 Service(const Service&)=delete;Service& operator=(const Service&)=delete;
 // Dedicated process-owned JVMTI environment keeps legacy deoptimization
 // enabled while any evolved final permissions may require this listener.
 // Breakpoint callback is empty; its isolated fixture method owns the lease.
 bool PrepareEnvironment(JNIEnv*,std::string*);
 // Ordinary JNI/JVMTI stage, before Bind and exclusive listener installation.
 bool PrepareProofMethod(jmethodID fixture_writer,std::string*);
 bool StageFixtureField(JNIEnv*,jclass,jfieldID,bool is_static,std::string*);
 bool RegisterStrong(JNIEnv*,jobject,std::string*);
 void ForgetStrong(jobject);
 bool Bind(HookEngine&,void* observed_instrumentation,std::string*);
 bool InstallAtExclusiveBoundary(std::string*);
 // Borrowed native oracle only inside a calibrated mutator callback; exact
 // public IDs, initialized fixture Classes and native declarations must agree.
 // Installed primitive/reference fixture descriptors select the required real
 // FieldWritten overload; no arbitrary public field may authorize a proof.
 bool FixtureField(void* thread,jclass strong_target,jclass strong_writer,
   jmethodID,jfieldID,bool is_static,final_write_guard::Oracle*,uint32_t*,std::string*)const;
 bool SetFixtureFlags(const final_write_guard::Oracle&,uint32_t expected,uint32_t desired,std::string*);
 bool BeginProof(const final_write_guard::Oracle&);
 bool EndProof(bool illegal_access_error,bool unchanged_sentinel);
 // Actual public schema observation; no private ABI guess is a substitute.
 bool Stage(JNIEnv*,jvmtiEnv*,const std::vector<jclass>& old_classes,
   const std::vector<jclass>& replacement_classes,std::string*);
 // Collected refundable window: discover unsupported permissions and prepare
 // all alias storage before outer ART Run becomes irreversible.
 bool PreparePermissions(const source_commit::Plan& class_plan,std::string*);
 // Final native-only scope binds complete instances/origins to the same live
 // staged public Classes and immutable native field arenas (GC-safe anchors).
 bool Seal(const source_commit::Plan&,std::string*)const;
 bool RefreshPlan(const source_commit::Plan&,std::string*)const override;
 bool VerifyAliases(const source_commit::Plan&,std::string*)const override;
 bool Verify(const source_commit::Plan&,std::string*)const override;
 bool Verify(const source_commit::Plan&,size_t,void*,const evolution::ClassSchema&,
   const evolution::ClassSchema&,const native_schema_policy::Requirement&,std::string*)const override;
 // Publish preallocated aliases BEFORE permission publication; original ART
 // then restores native old owners and retains old metadata in ClassExt before
 // releasing the exclusive scope. This owner never borrows Holder root slots.
 void BeginCommit(const source_commit::Plan&)const override;
 // Abort roots are released; committed aliases own only weak Class references.
 // Dead weak anchors are deactivated before their JNI handles are deleted.
 void Clear(JNIEnv*);
 private:
#ifdef ARTDCEVM_TESTING
 friend struct TestPeer;
#endif
 friend bool final_field_calibration::Run(JNIEnv*,const final_field_calibration::Bindings&,std::string*);
 bool AlreadyCalibrated()const;
 void AbortCalibration();
 bool CompleteCalibration();
 struct State;std::unique_ptr<State> state_;
};
} // namespace artdcevm::final_field_service
