#pragma once
#include "platform/common/runtime_thread.h"
#include "platform/common/interface_redefinition.h"

#include <android/log.h>
#include <android/api-level.h>
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <limits>
#include <optional>
#include <utility>
#include <unordered_map>
#include <string_view>
#include "platform/common/fault_injection.h"
#include "platform/abi/arm64/interface_runtime_table.h"
#include "platform/abi/arm64/common/class_status.h"
#include "platform/abi/arm64/common/class_linker.h"
#include "platform/abi/arm64/common/root_visitor.h"
#include "platform/abi/arm64/common/handle_scope_visitor.h"
#include "platform/abi/arm64/common/jni_ids.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/art_hook.h"
#include "platform/common/class_layout.h"
#include "platform/common/interface_snapshot.h"
#include "platform/common/interface_array_roots.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::interface_redefinition {
using namespace abi::arm64;
template <class T> T& Field(void* object, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
using Admission = jvmtiError (*)(Handle, std::string*);
using Mirror = void* (*)(ClassRedefinitionPrefix*);
using Ensure = bool (*)(void*, void*, Handle, bool, bool);
using LookupReferrer = void* (*)(void*, TypeIndex, void*);
using LookupCache = void* (*)(void*, TypeIndex, void*, void*);
using LookupChars = void* (*)(void*, void*, const char*, void*);
using LookupView = void* (*)(void*, void*, std::string_view, void*);
using CacheAlloc = void* (*)(ClassRedefinitionPrefix*, Handle);
using SetCache = void (*)(void*, void*);
using Allocate = void* (*)(ClassRedefinitionPrefix*, Handle, Handle, Handle, uint16_t);
struct Process {
  LookupChars lookup_chars = nullptr;
  LookupView lookup_view = nullptr;
  CacheAlloc allocate_cache = nullptr;
  ClassArrayRoots::DecodeGlobal decode_global = nullptr;
  SetCache set_cache = nullptr;
  Allocate allocate_class = nullptr;
  void* original_lookup_referrer = nullptr;
  void* original_lookup_cache = nullptr;
  Mirror mirror_class = nullptr;
  ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
  Ensure ensure_initialized = nullptr;
  jobject (*new_local_ref)(JNIEnv*, void*) = nullptr;
  void (*delete_local_ref)(JNIEnv*, jobject) = nullptr;
  std::atomic<void*> class_linker{nullptr};
  void* original_normal[2]{};
  void* original_structural[2]{};
  void* original_finish = nullptr;
  void* original_run = nullptr;
  void* original_link_class = nullptr;
  void* original_non_public = nullptr;
  void (*set_status_internal)(void*, ClassStatus) = nullptr;
  bool available = false;
  std::atomic<const char*> disabled_reason{nullptr};
  std::atomic<int> verified_release{0};
  HandleScopeVisitors visit_handle_roots;
  std::unique_ptr<Backend> backend;
};
struct RuntimeBackend final : Backend {
  bool Available() override;
  bool AbstractLinkErrorsAvailable() override;
  Timing LastTiming() override;
  std::string RuntimeState() override;
  std::string TakeRejection() override;
  void ObserveClassLinker(void* linker) override;
  void Begin(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) override;
  void Describe(jclass klass, const evolution::ClassSchema& live) override;
  jvmtiError Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error) override;
  const std::vector<jclass>* GroupTypes(jclass klass) override;
  bool IsTarget(abi::arm64::ClassRedefinitionPrefix* self) override;
  bool IsAffected(abi::arm64::ClassRedefinitionPrefix* self) override;
  bool SharesImplementationGroup(abi::arm64::ClassRedefinitionPrefix* self) override;
  void* VerificationClass(void* linker, std::string_view descriptor, abi::arm64::Handle loader) noexcept override;
  bool CompleteInstances(abi::arm64::ClassRedefinitionPrefix*, abi::arm64::RedefinitionDataIter*,
                         bool (*)(abi::arm64::ClassRedefinitionPrefix*, abi::arm64::RedefinitionDataIter*)) noexcept override;
  jvmtiError VerificationError() noexcept override;
  bool Check(const runtime_thread::VerifiedThread&, abi::arm64::ClassRedefinitionPrefix*,
             const abi::arm64::RedefinitionDataIter&, std::string*) override;
};
namespace detail {
// Created by the first Prepare and kept for the process lifetime: installed
// hooks may still be on an ART stack after Install rolls back or degrades.
extern Process* process;
struct Target { jclass klass; bool boot; jint status; evolution::ClassSchema live; size_t group = 0; };
// The array order with one candidate owner first, as ART's holder slots
// require. ART's CollectAndCreateNewInstances collects only instances of the
// owner entry's class; `roots` lists the owner, then every other root, and
// each type's first root is the earliest one it is assignable to. A root in
// `rounds` is the first root of a concrete member: its instances are
// collected again by ART with that root as the entry's class.
//
// With Proxy implementations, an interface root would also collect the proxy
// instances, for which ART finds no replacement class: `roots` are then the
// rebuild's topmost classes, every round including the first is collected
// with its root as the entry's class, and interfaces have no first root (-1).
struct Ordering {
  jclass owner;
  TypeSnapshot snapshot;
  std::vector<jclass> roots;
  std::vector<int32_t> first_root;
  std::vector<int32_t> rounds;
  bool class_roots = false;
};
// One rebuild's types (leader first) and their Native-created private arrays.
struct Members {
  TypeSnapshot snapshot;
  std::vector<Ordering> orderings;  // One per candidate owner, covering ones first.
  std::unique_ptr<ClassArrayRoots> arrays;
};
// One interface rebuild: every interface target whose loaded subtypes overlap
// another's. Batch classes outside those type relations that are superclasses
// of a member are ancestors. ART would rebuild a structural ancestor's subtree
// itself, so the group then absorbs that subtree and rebuilds everything once.
struct Group {
  size_t leader;                 // The first interface root in batch order.
  std::vector<jclass> ancestors;
  Members core;                  // The interface roots' subtypes.
  Members extended;              // With ancestor subtrees; empty without ancestors.
  std::optional<bool> uses_extended;  // Fixed by the group's allocation.
  int32_t owner = -1;                 // Index into the chosen members' orderings.
  bool allocating = false;            // Detects rebuilds that link each other.
  // Before allocation chooses, the widest candidate membership.
  const Members& Current() const {
    return uses_extended ? (*uses_extended ? extended : core) : ancestors.empty() ? core : extended;
  }
  // After allocation: the array order of the chosen owner.
  const TypeSnapshot* Allocated() const {
    return owner < 0 ? nullptr : &Current().orderings[owner].snapshot;
  }
};
// Run's caller owns the DexFiles. This root outlives Run's holder and keeps
// every linked replacement reachable until the result is known, including
// partially completed allocations that never reach the holder's new_classes.
struct NonPublicFailure { interface_dispatch::NonPublicSelection selection; int32_t index = -1; };
struct Attempt { RedefinerPrefix* driver; std::vector<Handle> replacements; Handle pending; bool committed = false; jvmtiError error = JVMTI_ERROR_NONE; NonPublicFailure non_public; };
struct Linking {
  const std::vector<Handle>* old_types;
  Handle new_types;
  Handle private_cache;
  int32_t index;
};
// This module's part of a transaction context.
struct State final : transaction::Part {
  State(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions)
      : jni(jni), env(env), batch_size(count), definitions(definitions) {}
  ~State() override {
    if (class_class) jni->DeleteLocalRef(class_class);
    if (object_class) jni->DeleteLocalRef(object_class);
  }
  std::vector<Target> targets;
  std::vector<Group> groups;
  JNIEnv* jni;
  jvmtiEnv* env;
  jint batch_size;
  const jvmtiClassDefinition* definitions;
  // Runtime declarations selected for this transaction's thread and targets.
  const InterfaceRuntime* runtime = nullptr;
  std::optional<runtime_thread::VerifiedThread> thread = runtime_thread::Ready();
  const RuntimeThread* thread_layout = nullptr;
  bool runtime_verified = false;
  int selected_release = 0;
  JavaVM* vm = nullptr;
  jclass class_class = nullptr;
  jclass object_class = nullptr;
  const Target* rejected_target = nullptr;
  const char* rejection = nullptr;
  const std::string* rejection_class = nullptr;
  // ART's Run of this transaction, and the class it is linking for a group.
  Attempt* attempt = nullptr;
  const Linking* linking = nullptr;
  // ART allocates a class that links rebuilt interfaces: its lookups of
  // their old classes resolve to the allocated replacements.
  bool external = false;
};
inline State* CurrentState() {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::interfaces);
}
inline const InterfaceRuntime& Runtime() { return *CurrentState()->runtime; }
inline const runtime_thread::VerifiedThread& Thread() { return *CurrentState()->thread; }
inline const RuntimeThread& ThreadLayout() { return *CurrentState()->thread_layout; }
extern thread_local Timing timing;
struct JniFailure {};
class LocalRef {
 public:
  explicit LocalRef(void* object) : reference_(process->new_local_ref(CurrentState()->jni, object)) {}
  ~LocalRef() { process->delete_local_ref(CurrentState()->jni, reference_); }
  jobject Get() const { return reference_; }
 private:
  jobject reference_;
};
inline void* Decode(jclass klass) {
  RedefinerPrefix driver{nullptr, 0, nullptr, runtime_thread::Current(Thread())};
  ClassRedefinitionPrefix lookup{&driver, klass, nullptr};
  return process->mirror_class(&lookup);
}
inline Target* Find(void* klass) {
  State* state = CurrentState();
  if (!state) return nullptr;
  for (auto& target : state->targets)
    if (Decode(target.klass) == klass) return &target;
  return nullptr;
}
inline ClassStatus Status(void* klass) {
  // mirror/class.h: status occupies the four most significant bits.
  return static_cast<ClassStatus>(__atomic_load_n(&Field<uint32_t>(klass, Runtime().status),
                                                __ATOMIC_ACQUIRE) >> kClassStatusShift);
}
inline jvmtiError Reject(std::string* message, jvmtiError error, const char* why) {
  CurrentState()->rejection = why;  // Static reason; formatting waits until Native return.
  *message = why;
  return error;
}

void Disable(const char*) noexcept;
// Named services are prepared and no runtime probe disabled the backend.
bool RuntimeAvailable();
Handle ReplacementRoot(const Group&);
// The group whose current membership contains `klass`.
const Group* AffectedGroup(void*);
// The replacement an allocated rebuild holds for an old class, or null.
void* ReplacementFor(void* old, bool* pending);
jvmtiError NormalCan(Handle, std::string*) noexcept;
jvmtiError NormalGet(Handle, std::string*) noexcept;
jvmtiError StructuralCan(Handle, std::string*) noexcept;
jvmtiError StructuralGet(Handle, std::string*) noexcept;
bool Finish(ClassRedefinitionPrefix*, RedefinitionDataHolderPrefix&, RedefinitionDataIter*) noexcept;
jvmtiError Run(RedefinerPrefix*) noexcept;
bool LinkClass(void* linker, void* thread, const char* descriptor, Handle klass,
               Handle interfaces, Handle* output) noexcept;
void NonPublicImplementation(void*, ArtMethod*, ArtMethod*) noexcept;
void* LookupByReferrer(void*, TypeIndex, void*) noexcept;
void* LookupByCache(void*, TypeIndex, void*, void*) noexcept;
}  // namespace detail
}  // namespace artdcevm::interface_redefinition
