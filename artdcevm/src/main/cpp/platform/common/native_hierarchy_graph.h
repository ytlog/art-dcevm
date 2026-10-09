#pragma once
#include "evolution/class_schema.h"
#include "platform/common/prepared_root_bindings.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/source_commit.h"
#include <jvmti.h>
#include <memory>
#include <optional>
namespace artdcevm::modern_field_metadata { class FieldMetadata; struct ReadContext; }
namespace artdcevm::native_hierarchy_graph {
struct Input { jclass strong_original=nullptr; evolution::ClassSchema before,after; };
// Rooted public graph. Indices are stable throughout one operation. Proposed
// edges refer to the original identities that ART must actually replace.
struct Node {
 jclass strong_class=nullptr; jobject strong_loader=nullptr;
 void* klass=nullptr; void* loader=nullptr; void* replacement=nullptr;
 std::string descriptor; uint32_t flags=0;
 // Absent for implicit descendants: their declarations retain original flags.
 std::optional<uint32_t> proposed_flags;
 // JVMTI applies InnerClass annotation visibility. Preserve that independent
 // public oracle separately from ART effective Class access flags.
 std::optional<uint32_t> public_flags;
 uint32_t ProposedFlags()const noexcept{return proposed_flags.value_or(flags);}
 size_t parent=SIZE_MAX,proposed_parent=SIZE_MAX;
 std::vector<size_t> interfaces,proposed_interfaces;
 bool initialized=false,affected=false;
};
struct Graph { std::vector<Node> nodes; std::vector<size_t> inputs; };
struct Prepared {
 void* source_owner=nullptr; void* old_class=nullptr; void* new_class=nullptr;
 void* cache=nullptr; void* parent=nullptr; uint16_t class_index=0;
 const evolution::ClassSchema* declarations=nullptr;
 // Stable native slot identity from the real allocator Pool. Managed pointer
 // snapshots and copied declarations cannot distinguish same-descriptor classes
 // in different loaders after GC; the owner must validate this exact used slot.
 const void* source_old_root=nullptr;
};
// An installed source allocator/receiver/type-cache owner must independently
// prove each stage. A symbol, supported-version bit or snapshot is not a grant.
// None of these proof callbacks may mutate ART. Final callbacks may not allocate,
// invoke JNI, parse DEX, or suspend. The actual source owner consumes the staged
// graph and retains all early linked Classes before a fallible IDs allocation.
class Owner {
 public:
 virtual ~Owner()=default;
 virtual bool VerifyStaged(const Graph&,const std::vector<Input>&,std::string*)const=0;
 // Independent read-only proof of the actual unallocated source record.
 virtual bool VerifyAllocation(const Graph&,const Prepared&,std::string*)const{return false;}
 virtual bool VerifyPrepared(const Graph&,const Prepared&,std::string*)const=0;
 virtual bool Verify(const Graph&,const source_commit::Plan&)const noexcept=0;
 virtual void BeginCommit(const Graph&,const source_commit::Plan&)const noexcept=0;
};
class AllocationOwner:public Owner {
 public:
 virtual bool RefreshAllocations(Graph*,std::vector<Prepared>*)const noexcept=0;
 virtual bool RefreshPending(Prepared*)const noexcept{return false;}
};
class ReceiverOwner:public Owner {};
class CacheOwner:public Owner {
 public:
 // Explicit fallible incoming-cache mutation after capture/linking, separate
 // from the read-only proof callbacks. Published caches must stay read-only.
 virtual bool PrepareLinked(const Graph&,const Prepared&,std::string*){return false;}
};
struct Bindings {
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 void* decoder_owner=nullptr;
 bool (*borrow_owned)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 const AllocationOwner* allocation=nullptr; const ReceiverOwner* receivers=nullptr; CacheOwner* caches=nullptr;
};
// Pure graph validation used by the public stage after exact JNI identities are
// rooted. Rejects cycles across batch/unmodified edges, role/final/access errors,
// duplicate edges and uninitialized new parents. It grants no source permission.
bool ValidateGraph(Graph*,std::string*);
// Runnable-only after exact Class roots are borrowed. Verify non-visibility
// public modifiers against calibrated fixed metadata before adopting raw flags.
bool CanonicalizeFlags(const modern_field_metadata::FieldMetadata&,Graph*,std::string*);
class Authority final {
 public:
 Authority(); ~Authority();
 Authority(const Authority&)=delete;Authority& operator=(const Authority&)=delete;
 // Caller is an actual ordinary Runnable JNI owner. Globals remain live until
 // Clear after the entire original source transaction and its cleanup finish.
 bool Stage(const runnable_jni::Context&,jvmtiEnv*,const std::vector<Input>&,const Bindings&,std::string*);
 // Exact original Class and immutable incoming declarations only. Allows the
 // hierarchy comparisons or final/abstract mismatch; CheckRedefinable remains
 // original. The expected diagnostic grants no final Plan guard permission.
 bool CheckClass(const runnable_jni::Context&,jclass strong_original,const evolution::ClassSchema&,jvmtiError* expected_failure=nullptr)noexcept;
 bool AuthorizeAllocation(const runnable_jni::Context&,const Prepared&,std::string*);
 bool PrepareLinked(const runnable_jni::Context&,Prepared*,std::string*);
 bool VerifyPrepared(const runnable_jni::Context&,const Prepared&,std::string*);
 // Ordinary Collected root-layout capture. This freezes native pointer slots
 // for preregistration but grants no graph or commit permission.
 bool PrepareRoots(const source_commit::Plan&,std::string*);
 void UseReadContext(const modern_field_metadata::ReadContext&) noexcept;
 // Collected source owner window, after all actual early allocations/linking.
 // After PrepareRoots/registration this validates without copying containers.
 bool Seal(const source_commit::Plan&,std::string*);
 bool Verify(const source_commit::Plan&,size_t pair)const noexcept;
 bool CollectManagedRoots(const prepared_roots::Registrar&);
 bool CanRefresh(const prepared_roots::Resolver&)const noexcept;
 void Refresh(const prepared_roots::Resolver&)noexcept;
 void BeginCommit(const source_commit::Plan&)const noexcept;
 void Clear(JNIEnv*);
 const Graph* Staged()const noexcept;
 private:
 struct State;std::unique_ptr<State> state_;
};
}
