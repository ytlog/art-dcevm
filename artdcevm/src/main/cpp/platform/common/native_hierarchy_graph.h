#pragma once
#include "evolution/class_schema.h"
#include "platform/common/bounded_managed_roots.h"
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
struct RootWitness {
 const bounded_managed_roots::Store* owner=nullptr;bounded_managed_roots::Slot slot{};uint64_t generation=0;
 bool Valid()const noexcept{return owner&&generation==owner->Generation()&&owner->Valid(slot);}
 bool operator==(const RootWitness&r)const noexcept{return owner==r.owner&&generation==r.generation&&slot.chunk==r.slot.chunk&&slot.index==r.slot.index;}
 bool operator!=(const RootWitness&r)const noexcept{return !(*this==r);}
};
struct Node {
 // Only independently owned source inputs require a separate global Class.
 jclass strong_class=nullptr; jobject strong_loader=nullptr;
 RootWitness class_root{},loader_root{},proxy_name_root{};
 void* proxy_name=nullptr;std::string proxy_name_text,negative_proxy_name;
 bool RootedClass()const noexcept{return class_root.owner?class_root.Valid():strong_class!=nullptr;}
 bool RootedLoader()const noexcept{return loader_root.owner?loader_root.Valid():strong_loader!=nullptr;}
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
struct Graph {
 std::vector<Node> nodes; std::vector<size_t> inputs;
 // ValidateGraph establishes a stable node index, never a cached managed
 // pointer. Authority owns immutable graph shape after validation; GC updates
 // the rooted identities in those same slots. Unvalidated graphs use the full
 // uniqueness scan. A changed shape must be validated again.
 std::optional<size_t> BootstrapObjectIndex()const noexcept{return indexed_count_==nodes.size()?bootstrap_object_:std::nullopt;}
 private:
 friend bool ValidateGraph(Graph*,std::string*);
 std::optional<size_t> bootstrap_object_;size_t indexed_count_=0;
};
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
class ReceiverOwner:public Owner {
 public:
 // Collected Seal freezes structural identity. Final publication additionally
 // consumes concrete same-Plan lifetime/dispatch/retained-cell recovery proofs.
 virtual bool VerifyFinal(const Graph&g,const source_commit::Plan&p)const noexcept{return Verify(g,p);}
};
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
 bool (*borrow_pair)(void*,const runnable_jni::Context&,jobject,jobject,void**,void**)=nullptr;
};
// Pure graph validation used by the public stage after exact JNI identities are
// rooted. Rejects cycles across batch/unmodified edges, role/final/access errors,
// duplicate edges and uninitialized new parents. It grants no source permission.
bool ValidateGraph(Graph*,std::string*);
// Runnable-only after exact Class roots are borrowed. Verify non-visibility
// public modifiers against calibrated fixed metadata before adopting raw flags.
bool CanonicalizeFlags(const modern_field_metadata::FieldMetadata&,Graph*,std::string*,const modern_field_metadata::ReadContext* =nullptr);
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
 // Collected preparation checks the complete sealed graph without claiming
 // original-final receiver/storage/frame permission.
 bool VerifyPrepared(const source_commit::Plan&,size_t pair)const noexcept;
 bool VerifyFinal(const source_commit::Plan&,size_t pair)const noexcept;
 // Compatibility: existing commit callers retain final semantics.
 bool Verify(const source_commit::Plan&,size_t pair)const noexcept;
 bool CollectManagedRoots(const prepared_roots::Registrar&);
 bool CanRefresh(const prepared_roots::Resolver&)const noexcept;
 void Refresh(const prepared_roots::Resolver&)noexcept;
 void BeginCommit(const source_commit::Plan&)const noexcept;
 void Clear(JNIEnv*);
 const Graph* Staged()const noexcept;
 private:
 bool VerifyPlan(const source_commit::Plan&,size_t pair,bool final)const noexcept;
 struct State;std::unique_ptr<State> state_;
};
}
