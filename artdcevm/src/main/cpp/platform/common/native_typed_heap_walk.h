#pragma once
#include "api/hook_engine.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/modern_field_metadata.h"
#include <cstddef>
#include "platform/abi/arm64/common/runtime_root_visit.h"
#include "platform/common/native_source_stop.h"
namespace artdcevm::native_typed_heap_walk {
// Fixed AOSP source declarations, not a copied Heap or lambda layout.
inline constexpr char kVerifyHeap[]="_ZN3art2gc4Heap20VerifyHeapReferencesEb";
inline constexpr char kVisitorCtor[]="_ZN3art2gc22VerifyReferenceVisitorC2EPNS_6ThreadEPNS0_4HeapEPmb";
inline constexpr char kVerifyReference[]="_ZNK3art2gc22VerifyReferenceVisitor15VerifyReferenceEPNS_6mirror6ObjectES4_NS_12MemberOffsetE";
enum class RootRepresentation {Pointer,Compressed};
struct RootRecord {const void* slot;void* value;uint32_t type,thread_id;RootRepresentation representation;
 // Only valid synchronously inside RootSink::Root; stack roots can use a
 // temporary pointer slot, never treat slot as its compressed physical spill.
 const void* original_frame_visitor=nullptr;size_t vreg=SIZE_MAX;
};
class RootSink {public:virtual ~RootSink()=default;virtual bool Root(const RootRecord&)noexcept=0;};
struct RootReport {enum class Failure {None,Binding,Owner,Memory,Lock,Original,Sink};Failure failure=Failure::Binding;size_t pointer_roots=0,compressed_roots=0;bool completed=false;};
// Diagnostic source observation only. Neither Bind nor a completed walk is a
// public-oracle calibration, no-escape proof, or narrowing permission. Borrowed
// root slots are valid only within the synchronous original visitor callback.
class RootProvenance final {
 public:
 bool Bind(HookEngine&,void* independently_bound_runtime,void*(*current)(),std::string*);
 // Uses pinned source kGenericBottom; no guessed/default ordinal, Runtime
 // layout read, caller-provided level, or API selection.
 // This private-lock oracle runs before a SourceStop, in real Runnable context.
 bool CalibrateLocks(const runnable_jni::Context&,std::string*);
 bool LocksCalibrated()const noexcept{return locks_calibrated_;}
 RootReport Observe(const native_source_stop::Session&,void* self,
     const modern_field_metadata::ReadContext&,RootSink&)const noexcept;
 // Complete AllRoots source stream with GC masks and original frame cursor,
 // independent of optional Dex register maps. This is not a recovery grant.
 RootReport ObserveFrameRoots(const native_source_stop::Session&,void* self,
     const modern_field_metadata::ReadContext&,RootSink&)const noexcept;
 private:
 RootReport ObserveImpl(const native_source_stop::Session&,void*,const modern_field_metadata::ReadContext&,RootSink&,bool)const noexcept;
 using OwnerTid=int32_t(*)(const void*);using SharedHeld=bool(*)(const void*,const void*);
 void* runtime_=nullptr;void*(*current_)()=nullptr;
 abi::arm64::runtime_root_visit::Visit visit_=nullptr;void*(*barrier_)(void*)=nullptr;
 int32_t(*tid_)()=nullptr;OwnerTid mutex_owner_=nullptr,rw_owner_=nullptr;SharedHeld shared_=nullptr;
 void** class_lock_=nullptr;void** trace_lock_=nullptr;bool locks_calibrated_=false;size_t mutex_owner_offset_=0,rw_owner_offset_=0,rw_state_offset_=0;
 abi::arm64::runtime_root_visit::MutexConstruct mutex_construct_=nullptr;
 abi::arm64::runtime_root_visit::RwConstruct rw_construct_=nullptr;
 abi::arm64::runtime_root_visit::Destroy mutex_destroy_=nullptr,rw_destroy_=nullptr;
 abi::arm64::runtime_root_visit::Lock mutex_lock_=nullptr,mutex_unlock_=nullptr,rw_lock_=nullptr,rw_unlock_=nullptr;
};
class Sink {
 public:virtual ~Sink()=default;
 virtual bool Reference(void* object,void* value,uint32_t source_offset)noexcept=0;
};
class Service final {
 public:
 bool Bind(HookEngine&,void* independently_bound_heap,void* (*current)(),std::string*);
 // Public JNI supplies two distinct array values. The real exclusive service
 // reloads the three globals; the original whole-heap verifier must report both
 // precise field locations plus each real root before readiness is published.
 bool Calibrate(JNIEnv*,jobjectArray,jobject,jobject,const exclusive_owned_roots::Service&,std::string*);
 // These caller-owned strong globals supply an instance and static reference
 // field of the same independently calibrated declaration. Public JNI and the
 // exact named reflection leaf must agree before entering the real pause.
 bool CalibrateFields(HookEngine&,JNIEnv*,jobject instance,jclass owner,
     jobject reflected_instance,jobject reflected_static,jobject left,jobject right,
     const modern_field_metadata::FieldMetadata&,const exclusive_owned_roots::Service&,std::string*);
 bool Calibrated()const noexcept{return calibrated_&&fields_calibrated_;}
 bool BoundTo(void* heap,void*(*current)())const noexcept{return heap_==heap&&current_==current&&verify_;}
 bool ArrayCalibrated()const noexcept{return calibrated_;}
 bool FieldsCalibrated()const noexcept{return fields_calibrated_;}
 // Caller supplies its actual source/exclusive/disabled-GC owner separately;
 // this service establishes no pause or source permission.
 bool Walk(void* actual_self,Sink&)const noexcept;
 private:
 using Verify=size_t(*)(void*,bool);using Ctor=void(*)(void*,void*,void*,size_t*,bool);
 using Reference=bool(*)(const void*,void*,void*,uint32_t);
 struct Episode;static thread_local Episode* current_episode_;
 static Ctor original_ctor_;static Reference original_reference_;
 static void Construct(void*,void*,void*,size_t*,bool);
 static bool Observe(const void*,void*,void*,uint32_t);
 static bool ProbeFields(const exclusive_owned_roots::Context&,void*);
 static bool Probe(const exclusive_owned_roots::Context&,void*);
 bool WalkUncalibrated(void*,Sink&)const noexcept;
 void* heap_=nullptr;void* (*current_)()=nullptr;Verify verify_=nullptr;bool calibrated_=false,fields_calibrated_=false;
};
}
