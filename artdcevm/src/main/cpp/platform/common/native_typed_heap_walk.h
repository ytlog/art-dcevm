#pragma once
#include "api/hook_engine.h"
#include "platform/common/exclusive_owned_roots.h"
#include "platform/common/modern_field_metadata.h"
#include <cstddef>
namespace artdcevm::native_typed_heap_walk {
// Fixed AOSP source declarations, not a copied Heap or lambda layout.
inline constexpr char kVerifyHeap[]="_ZN3art2gc4Heap20VerifyHeapReferencesEb";
inline constexpr char kVisitorCtor[]="_ZN3art2gc22VerifyReferenceVisitorC2EPNS_6ThreadEPNS0_4HeapEPmb";
inline constexpr char kVerifyReference[]="_ZNK3art2gc22VerifyReferenceVisitor15VerifyReferenceEPNS_6mirror6ObjectES4_NS_12MemberOffsetE";
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
