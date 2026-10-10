#pragma once
#include "platform/common/native_receiver_guard.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/thread_suspend_service.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/abi/common/jni_id_query_scope.h"
#include <atomic>
namespace artdcevm::receiver_guard {
// Read leaves for the actual original Runnable/shared-mutator ingress. An OS
// mapping, Thread state or successful Bind never establishes this borrow.
class EntrySource final {
 public:
 struct Binding {
  void* owner=nullptr;JavaVM* vm=nullptr;
  void* (*current)()=nullptr;
  // Independent calibrated owned strong decoder; ordinary calibration only.
  bool (*strong_oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
  bool (*with)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
  // Context made in the live ordinary callback; bounds only, no OS fallback.
  modern_field_metadata::ReadContext (*bounds)(void*,const runnable_jni::Context&)=nullptr;
  void* jni_id_manager=nullptr;
 };
 struct Samples {
  // Public reflected Method/Field, one instance and one static of each.
  jobject methods[2]{},fields[2]{},receivers[2]{};
  // Optional actual original JNI argument (not a fabricated slot). Public JNI
  // must independently report LocalRef and identity with receivers[0].
  jobject transition_reference=nullptr;
 };
 bool Bind(HookEngine&,Binding,const modern_field_metadata::FieldMetadata&,
           const thread_suspend_service::Service&,std::string*);
 bool Calibrate(JNIEnv*,const Samples&,std::string*);
 bool Proven()const noexcept{return ready_.load(std::memory_order_acquire);}
 bool TransitionProven()const noexcept{return Proven()&&transition_;}
 // Construct only at the unique original Runnable/shared-mutator callback:
 // its live args/root slots retain managed values until this stack scope exits.
 // The context is that ingress's actual Context, never recreated from bounds.
 // No JNI, suspension or relocation may occur while a borrowed value is used.
 class InvocationScope final {
  public:
   InvocationScope(EntrySource&,const runnable_jni::Context&,modern_field_metadata::ReadContext)noexcept;
   ~InvocationScope();
   bool Valid()const noexcept{return valid_;}
   InvocationScope(const InvocationScope&)=delete;
   InvocationScope&operator=(const InvocationScope&)=delete;
  private:
   friend class EntrySource;
   EntrySource& source_;runnable_jni::Context context_;modern_field_metadata::ReadContext bounds_;bool valid_=false;
 };
 // Only these leaves are supplied; relations and MethodHandle have independent
 // permanent owners/proofs. Proven() is not Guard::Proven().
 // Only the live original ScopedObjectAccessAlreadyRunnable argument may be
 // passed here. This is root provenance, not arbitrary Context/map permission.
 // The original wrapper retains its member and strong receiver; no safepoint,
 // JNI call or waiting decoder is permitted until the caller releases them.
 struct OriginalRoots {void* receiver=nullptr;void* declaring=nullptr;void* local_slot=nullptr;};
 bool SeedOriginalRoots(const abi::common::RunnableJniScope&,void* actual_method,
                        jobject actual_receiver,OriginalRoots*)const noexcept;
 bool OriginalScopeCurrent(const abi::common::RunnableJniScope&)const noexcept;
 bool OwnsMetadata(const modern_field_metadata::FieldMetadata& metadata)const noexcept{return Proven()&&metadata_==&metadata;}
 Access Leaves()noexcept;
 private:
 using Decode=abi::arm64::android11::ReleaseObjectPointer(*)(const void*,jobject);
 using Root=void*(*)(void*);
 using Throw=void(*)(void*,const char*,const char*);
 struct Proof;
 static bool Observe(const runnable_jni::Context&,void*);
 static bool Current(void*,void*)noexcept;
 static bool Member(void*,void*,bool,uint32_t*,void**)noexcept;
 static bool ObjectClass(void*,void*,void**)noexcept;
 static bool Reference(void*,void*,void*,void**)noexcept;
 static bool Compressed(void*,void*,uint32_t,void**)noexcept;
 static bool FieldId(void*,void*,void*,void**)noexcept;
 static void Raise(void*,void*,const char*,const char*)noexcept;
 bool Borrow(void*)const noexcept;
 bool Extent(const void*,size_t)const noexcept;
 bool ReadReference(void*,jobject,void**)const noexcept;
 bool ReadLocalSlot(void*,jobject,void**)const noexcept;
 Binding binding_{};thread_suspend_service::Service states_{};
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 Decode decode_=nullptr;Root root_=nullptr;Throw raise_=nullptr;
 abi::common::DecodeFieldId field_id_=nullptr;abi::common::DecodeMethodId method_id_=nullptr;
 bool bound_=false,transition_=false,inline_local_=false;std::atomic<bool> ready_{false};
 inline static thread_local InvocationScope* invocation_=nullptr;
};
}
