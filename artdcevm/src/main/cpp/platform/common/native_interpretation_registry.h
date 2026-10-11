#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <array>
#include "api/hook_engine.h"
#include <jni.h>
#include "platform/common/method_identity_calibration.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/abi/arm64/common/method_compilation_policy.h"

namespace artdcevm::native_selective_interpretation {class Service;struct Method;}
namespace artdcevm::native_source_stop {class Session;class Service;}
namespace artdcevm::bounded_managed_roots {class Store;}
namespace artdcevm::native_source_compilation {class Service;}
namespace artdcevm::runtime_services {struct Snapshot;}
namespace artdcevm::retirement {struct Owners;}
namespace artdcevm::source_handle_roots {class Service;}
namespace artdcevm::native_call_graph_execution {class Execution;}
namespace artdcevm::modern_field_metadata {class FieldMetadata;struct ReadContext;}
namespace artdcevm::native_interpretation_registry {
// Process-owned opaque identities only. No Class, ClassLoader, JNI root or
// allocator is retained. The named Instrumentation query joins this registry
// with ART's original set. Compiler and entry-update protection remain separate
// prerequisites; query calibration alone grants no execution permission.
class Registry final {
 public:
 using ContainsUnsafe=bool(*)(const void*,void*);
 class Reservation final {
  public:
  ~Reservation();
  Reservation(const Reservation&)=delete;Reservation&operator=(const Reservation&)=delete;
  private:
  friend class Registry;
  Reservation(Registry&owner):owner_(owner){}
  Registry&owner_;
  std::vector<void*>requested_,candidate_;
  bool valid_=false,committed_=false;
 };
 Registry()=default;~Registry();
 Registry(const Registry&)=delete;Registry&operator=(const Registry&)=delete;
 // Ordinary preparation allocates the entire merged storage before the source
 // stop. One outstanding reservation serializes publication; rejection never
 // removes entries retained by earlier successful operations. The producer
 // independently roots requested carriers until it commits or cancels; this
 // registry outlives every uncommitted reservation and its installed consumers.
 std::unique_ptr<Reservation>Prepare(const std::vector<void*>&,std::string*);
 bool Contains(void*)const noexcept;
 size_t Count()const noexcept;
 // BindQuery must first complete at the actual ordinary Native JNI boundary;
 // its independently observed owner is shared with both temporary journals.
 // Synchronous original Runnable bootstrap over two independently rooted
 // managed fixture methods in one independently owned Class. Initialization
 // owns the retaining Store/HandleScope and exposes it only in its actual stop.
 // The caller retains the public Class/mirror globals and source calibration.
 // Once any hook is installed this Registry must be process-owned even if a
 // later step refuses initialization.
 bool Initialize(HookEngine&,const runnable_jni::Context&,
     const method_identity::Calibration&,const modern_field_metadata::FieldMetadata&,
     const source_handle_roots::Service&,jclass owned_fixture,
     const method_identity::Oracle&positive,const method_identity::Oracle&negative,
     const runtime_services::Snapshot&,native_source_compilation::Service&,
     const native_source_stop::Service&,size_t self_extent,const retirement::Owners&,std::string*);
 // Ordinary preparation binds the independently observed runtime owner. Hook
 // publication and the rooted positive/negative probe require a genuine stop.
 // Once installed this Registry is process-owned and must outlive the runtime.
 bool BindQuery(HookEngine&,JNIEnv*,std::string*);
 bool InstallQuery(HookEngine&,const native_source_stop::Session&,void*,std::string*);
 bool CalibrateQuery(const native_source_stop::Session&,void*,
     const native_selective_interpretation::Service&positive,
     const native_selective_interpretation::Service&negative,
     const modern_field_metadata::ReadContext&,const char**failure)noexcept;
 bool BindCompilationPolicy(HookEngine&,const method_identity::Calibration&,
     const modern_field_metadata::FieldMetadata&,std::string*);
 bool InstallCompilationPolicy(HookEngine&,const native_source_stop::Session&,void*,std::string*);
 // Distinct managed probes in one rooted fixture Class. The real class-wide
 // operation must preserve the protected method and clear the unprotected one.
 // Other managed methods must initially have no DontCompile bit so the probe
 // does not change their state; both probe flags are restored before return.
 bool CalibrateCompilationPolicy(const native_source_stop::Session&,void*,
     const modern_field_metadata::FieldMetadata&,
     const native_selective_interpretation::Service&positive,
     const native_selective_interpretation::Service&negative,
     const modern_field_metadata::ReadContext&,const char**failure)noexcept;
 bool BindEntryQuery(HookEngine&,std::string*);
 // Fixed exported exclusive-mutator operations that can rebuild entrypoints.
 // Their original bookkeeping runs first; surviving protected entries are
 // then reinitialized through ART before the exclusive callback returns.
 bool BindEntryUpdates(HookEngine&,std::string*);
 bool InstallEntryUpdates(HookEngine&,const native_source_stop::Session&,void*,std::string*);
 // Derive the bridge from an actual owned Deoptimize, authenticate it through
 // the named ClassLinker predicate, then probe the hooked code query with a
 // distinct rooted negative. It exercises original Undeoptimize and entry
 // reinitialization after the native set entry is gone; temporary native
 // ownership and the fixture's compilation bit are restored before return.
 bool CalibrateEntryQuery(HookEngine&,const native_source_stop::Session&,void*,
     native_selective_interpretation::Service&positive,
     const native_selective_interpretation::Service&negative,
     const modern_field_metadata::ReadContext&,std::string*);
 // Sole calibrated allocator cleanup calls this before original destruction.
 // Membership examines opaque addresses only; no method is dereferenced. A
 // lost requested carrier invalidates its uncommitted reservation.
 size_t Prune(const void*allocator,ContainsUnsafe)noexcept;
 private:
 friend struct QueryHooks;
 friend struct CompilationHooks;
 friend struct EntryQueryHooks;
 friend struct EntryUpdateHooks;
 friend class native_selective_interpretation::Service;
 friend class native_call_graph_execution::Execution;
 using Query=bool(*)(void*,void*);
 // The transaction journals ART's native set through this exact owner-bound
 // original. Persistent membership must not masquerade as a foreign entry.
 static Query NativeQueryFor(void*runtime,void*instrumentation)noexcept;
 struct RootedProbes {void*positive=nullptr;void*negative=nullptr;};
 bool ValidateProbePair(const native_source_stop::Session&,void*,
     const native_selective_interpretation::Service&,
     const native_selective_interpretation::Service&,
     const modern_field_metadata::ReadContext&,RootedProbes*,const char**)const noexcept;
 // The call-graph execution owner must independently validate the complete
 // rooted selection and persistent entry protection in the same actual final
 // stop before publishing. Neither preparation nor Contains is such a proof.
 bool PreparedFor(const Reservation&,const std::vector<void*>&)const noexcept;
 bool ReadyFor(const native_selective_interpretation::Service&)const noexcept;
 bool Commit(const native_source_stop::Session&,void*self,Reservation&)noexcept;
 void ForceNonCompilable(void*)const noexcept;
 void RestoreProtectedEntries(void*instrumentation)const noexcept;
 void Cancel(Reservation&)noexcept;
 mutable std::mutex mutex_;
 std::vector<void*>methods_;
 Reservation*pending_=nullptr;
 void*runtime_=nullptr;void**runtime_slot_=nullptr;void*instrumentation_=nullptr;void*class_linker_=nullptr;
 Query query_target_=nullptr,original_query_=nullptr;
 void*query_token_=nullptr;bool query_calibrated_=false;
 modern_field_metadata::Model class_model_{};
 method_metadata::Layout method_layout_{};
 abi::arm64::android_common::method_compilation_policy::ClearClass clear_class_target_=nullptr,original_clear_class_=nullptr;
 abi::arm64::android_common::method_compilation_policy::ClearIntrinsic clear_intrinsic_=nullptr;
 void*compilation_token_=nullptr;bool compilation_calibrated_=false;
 using CodeQuery=const void*(*)(void*,void*);
 using Reinitialize=void(*)(void*,void*);
 using IsBridge=bool(*)(const void*,const void*);
 CodeQuery code_target_=nullptr,original_code_=nullptr;
 IsBridge is_bridge_=nullptr;const void*interpreter_bridge_=nullptr;
 Reinitialize reinitialize_=nullptr;
 void*code_token_=nullptr;bool code_calibrated_=false;
 using EntryUpdate=void(*)(void*);
 using KeyUpdate=void(*)(void*,const char*);
 using DisableUpdate=void(*)(void*,const char*,bool);
 using TraceUpdate=void(*)(void*,const char*,void*,bool);
 std::array<void*,7>update_targets_{},update_originals_{},update_tokens_{};
};
}
