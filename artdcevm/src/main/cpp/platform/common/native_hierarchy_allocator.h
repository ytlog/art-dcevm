#pragma once
#include "platform/common/source_allocation_transaction.h"
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/runtime_services.h"
#include "platform/abi/arm64/android-common/hierarchy_allocation.h"
#include <memory>
namespace artdcevm::native_hierarchy_allocator {
// Fixed named source boundaries are owned by the production hook adapter. The
// original trampolines (never proxies) must remain live for every Batch.
struct Functions {
 abi::arm64::android_common::HierarchyAllocator allocate=nullptr;
 abi::arm64::android11::ReleaseObjectPointer (*find)(void*,void*,const char*,void*)=nullptr;
 bool (*ensure_ids)(const void*)=nullptr;
 // Hook wrapper supplies the source-declared RecordFailure bridge. It must not
 // allocate/throw; no NDK std::string crosses the platform ABI.
 void (*failure)(void* source_owner,int error,const char* literal) noexcept=nullptr;
 void (*retire_status)(void*,uint8_t)=nullptr;
 abi::arm64::android_common::HierarchyLoadClass load_class=nullptr;
};
class Batch;
class Service final:public native_hierarchy_graph::AllocationOwner {
 public:
 static bool Bind(HookEngine&,const modern_field_metadata::FieldMetadata&,
   source_handle_roots::Service&,const runtime_services::Snapshot&,Service*,std::string*);
 // Bind exact original hook trampolines. Merely binding never grants graph
 // permission; ObserveOriginal proves source roots/FindClass/capture behaviour
 // on an original, unchanged-hierarchy structural operation first.
 bool SetOriginals(const Functions&,std::string*);
 // Public independent monitor oracle on an exclusively owned ordinary fixture.
 bool CalibrateRetirement(const runnable_jni::Context&,jobject strong_fixture,std::string*);
 void* ObserveOriginal(const runnable_jni::Context&,jobject strong_original,
   void* actual_owner,const void* old_handle,const void* parent_handle,
   const void* cache_handle,uint16_t index,std::string*);
 void CaptureAllocated(const void* source_class_handle);
 void CaptureLinked(const void* source_class_handle);
 bool EnsureInstanceIds(const void* source_class_handle)noexcept;
 void LoadClass(void* linker,void* self,const void* dex_file,const void* class_def,const void* source_class_handle)noexcept;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,
   const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool VerifyPrepared(const native_hierarchy_graph::Graph&,
   const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool Verify(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 // Uses only actual source Pool slots. Graph classes have been refreshed from
 // privately retained strong globals before this call. No parsing/JNI/allocation.
 bool RefreshAllocations(native_hierarchy_graph::Graph*,
   std::vector<native_hierarchy_graph::Prepared>*)const noexcept override;
 private:
 friend class Batch;
 const modern_field_metadata::FieldMetadata* metadata_=nullptr;
 source_handle_roots::Service* roots_=nullptr;
 HookEngine* hooks_=nullptr;void* class_linker_=nullptr;
 using Enter=abi::arm64::android11::ReleaseObjectPointer(*)(void*,abi::arm64::android11::ReleaseObjectPointer,bool);
 using Exit=bool(*)(void*,abi::arm64::android11::ReleaseObjectPointer);
 Enter enter_=nullptr;Exit exit_=nullptr;
 bool Retire(const runnable_jni::Context&,source_handle_roots::Pool&,void* old_root,void* new_root,void* cache_root,void* loader_root)const noexcept;
 Functions functions_{};bool originals_=false,observed_=false,monitor_=false;
 Batch* Current()const noexcept;
 bool CurrentSelf(void*)const noexcept;
};
// Construct/destroy strictly within the original named Redefiner::Run Runnable
// lifetime. Pool roots span ALL structural islands and source DEX cleanup.
class Batch final {
 public:
 Batch(Service&,const runnable_jni::Context&,native_hierarchy_graph::Authority&);
 ~Batch();Batch(const Batch&)=delete;Batch& operator=(const Batch&)=delete;
 bool Prepare(const transaction_root::Holder*,std::string*);
 void* Allocate(void* actual_owner,const void* old_handle,const void* cache_handle,
   uint16_t index,std::string*);
 void CaptureAllocated(const void* actual_class_handle);
 void CaptureLinked(const void* actual_class_handle);
 // The source owner must retire every captured temporary before the source
 // DEX disappears. No caller-owned published Class is ever a retirement target.
 bool Complete(bool committed,std::string*);
 struct RecordView {void* source_owner;uint16_t class_index;void* old_root;void* new_root;void* cache_root;void* parent_root;const evolution::ClassSchema* declarations;evolution::MappedDexMetadata dex;};
 size_t Count()const noexcept;
 bool At(size_t,RecordView*)const noexcept;
 void* Get(void* root)const noexcept;
 void* Find(const char* descriptor,void* loader)const;
 void* OriginalFind(const char* descriptor,void* loader_root)const;

 private:
 friend class Service;
 struct State;std::unique_ptr<State> state_;
};
}
