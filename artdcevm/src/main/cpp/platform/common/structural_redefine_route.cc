#include "platform/common/structural_redefine_internal.h"
#include "platform/common/constructor_history.h"
#include "platform/common/method_handle_lookup.h"

#include <android/api-level.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>
#include "platform/abi/arm64/class_definition_table.h"
#include "platform/common/art_hook.h"
#include "platform/common/fault_injection.h"
#include "platform/common/runtime_thread.h"
#include "platform/common/narrowed_references.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/verification_redirect.h"

namespace artdcevm::structural_redefine::detail {
// Methods each class retired in committed transactions, guarded by
// transaction_mutex. ART's reference replacement keeps each global reference
// on the class's current definition.
RetiredKinds* FindRetiredKinds(JNIEnv* jni, jclass klass) {
  for (RetiredKinds& entry : process->retired_kinds)
    if (jni->IsSameObject(entry.klass, klass)) return &entry;
  return nullptr;
}

void RecordRetiredKinds(JNIEnv* jni, jclass klass, const std::vector<evolution::Member>& methods) {
  RetiredKinds* entry = FindRetiredKinds(jni, klass);
  if (!entry) {
    jobject root = jni->NewGlobalRef(klass);
    if (!root) {
      // Without the record a later batch could give these methods another kind.
      if (jni->ExceptionCheck()) jni->ExceptionClear();
      Diagnose("cannot record the kinds of retired methods");
      return;
    }
    entry = &process->retired_kinds.emplace_back(RetiredKinds{root, {}});
  }
  for (const auto& method : methods) entry->methods[{method.name, method.descriptor}] = method.access;
}

// Targets of a routed transaction whose subclass the JVMTI ClassPrepare event
// reports after the engine's snapshot, on any thread.
struct Watch {
  std::vector<jobject> targets;  // Global references.
  std::atomic<bool> subclass_loaded{false}, any_class_prepared{false};
};
void JNICALL ClassPrepared(jvmtiEnv*, JNIEnv* jni, jthread, jclass klass) noexcept {
  std::lock_guard<std::mutex> lock(process->watch_mutex);
  for (Watch* watch : process->watches) {
    watch->any_class_prepared = true;
    for (jobject target : watch->targets)
      if (!jni->IsSameObject(klass, target) &&
          jni->IsAssignableFrom(klass, static_cast<jclass>(target)))
        watch->subclass_loaded = true;
  }
}

// Registers a watch for one transaction attempt. Attempts are serialized by
// transaction_mutex (nested ones run on the same thread), so only the
// outermost scope switches the event.
class WatchScope {
 public:
  WatchScope(transaction::Context* context, JNIEnv* jni, jint count,
             const jvmtiClassDefinition* definitions) : jni_(jni) {
    if (!process->capabilities.subclass_watch) return;
    for (jint i = 0; i < count; ++i)
      if (jobject target = jni->NewGlobalRef(definitions[i].klass)) watch_.targets.push_back(target);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    bool first;
    {
      std::lock_guard<std::mutex> lock(process->watch_mutex);
      first = process->watches.empty();
      process->watches.push_back(&watch_);
    }
    if (first) process->snapshot_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);
    BatchOf(context)->subclass_loaded = &watch_.subclass_loaded;
    BatchOf(context)->any_class_loaded = &watch_.any_class_prepared;
  }
  ~WatchScope() {
    if (!process->capabilities.subclass_watch) return;
    bool last;
    {
      std::lock_guard<std::mutex> lock(process->watch_mutex);
      process->watches.erase(std::find(process->watches.begin(), process->watches.end(), &watch_));
      last = process->watches.empty();
    }
    if (last) process->snapshot_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);
    for (jobject target : watch_.targets) jni_->DeleteGlobalRef(target);
  }
  WatchScope(const WatchScope&) = delete;
  WatchScope& operator=(const WatchScope&) = delete;
  bool SubclassLoaded() const { return watch_.subclass_loaded; }
  bool AnyClassPrepared() const { return watch_.any_class_prepared; }

 private:
  JNIEnv* jni_;
  Watch watch_;
};

// Owns one jvmtiEnv-allocated array of local class references.
class LocalClasses {
 public:
  LocalClasses(jvmtiEnv* env, JNIEnv* jni) : env_(env), jni_(jni) {}
  ~LocalClasses() {
    if (!classes) return;
    for (jint i = 0; i < count; ++i) jni_->DeleteLocalRef(classes[i]);
    env_->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  LocalClasses(const LocalClasses&) = delete;
  LocalClasses& operator=(const LocalClasses&) = delete;
  jint count = 0;
  jclass* classes = nullptr;

 private:
  jvmtiEnv* env_;
  JNIEnv* jni_;
};

std::string Signature(jclass klass) {
  char* signature = nullptr;
  if (!klass || process->snapshot_env->GetClassSignature(klass, &signature, nullptr) != JVMTI_ERROR_NONE)
    return "<invalid class>";
  std::string result = signature;
  process->snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
  return result;
}

std::string ErrorName(jvmtiError error) {
  std::string name = "JVMTI_ERROR_" + std::to_string(error);
  char* text = nullptr;
  if (process->snapshot_env->GetErrorName(error, &text) == JVMTI_ERROR_NONE) {
    name = text;
    process->snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(text));
  }
  return name + " (" + std::to_string(error) + ")";
}

// The rejection of a failure that neither the policy nor the engine
// explained. ART can reject an unmodifiable class at its entry before policy
// validation or ClassFileLoadHook runs, and keeps its own message in the log only.
std::string DescribeFailure(jint count, const jvmtiClassDefinition* definitions,
                            jvmtiError result) {
  std::string classes;
  for (jint i = 0; definitions && i < count; ++i) {
    const jclass klass = definitions[i].klass;
    const std::string signature = Signature(klass);
    jboolean flag = JNI_FALSE;
    if (result == JVMTI_ERROR_UNMODIFIABLE_CLASS && klass) {
      if (process->snapshot_env->IsModifiableClass(klass, &flag) == JVMTI_ERROR_NONE && !flag)
        return signature + ": ART reports the class as unmodifiable; " + ErrorName(result);
    }
    classes += (classes.empty() ? "" : ", ") + signature;
  }
  return (classes.empty() ? "" : classes + ": ") + "ART rejected the redefinition with " +
      ErrorName(result) + "; its reason follows \"FAILURE TO REDEFINE\" in the log";
}

jvmtiError InvokeStructural(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions,
                            transaction::Context* context, const Commit& commit) {
  const jvmtiError result = commit.call(commit.state, env, count, definitions);
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  if (std::string reason = interfaces ? interfaces->TakeRejection() : std::string{}; !reason.empty()) Diagnose(reason, true);
  // A rejection decided after allocation (CollectAndCreateNewInstances).
  if (std::string late = std::exchange(context->rejection, {}); !late.empty()) Diagnose(late, true);
  if (result == JVMTI_ERROR_NONE && count > 0) {
    Batch* batch = BatchOf(context);
    constructor_history::Committed(batch->jni, count, definitions);
    if (batch->retires_members || context->retain_method_aliases) {
      member_retirement::RetainRoots(std::move(batch->retirement_roots));
      batch->retirement_roots.clear();
    }
    for (const auto& [klass, snapshot] : batch->classes)
      if (!snapshot.retiring.empty()) RecordRetiredKinds(batch->jni, klass, snapshot.retiring);
    // A nested ClassFileLoadHook transaction may change the outer batch's
    // targets after its snapshots. Let ART's original validators handle that
    // outer batch rather than accepting a decision based on stale members.
    for (transaction::Context* outer = context->enclosing; outer; outer = outer->enclosing) {
      BatchOf(outer)->classes.clear();
      BatchOf(outer)->decisions.clear();
    }
  }
  return result;
}

// An obsolete method keeps running old code after the batch commits, and its
// next call re-resolves by name: with an invoke-kind change, that code could
// invoke the new member as the old kind. Record any batch class on a stack.
static jvmtiError MatchActiveClass(jvmtiEnv* env, JNIEnv* jni, jclass declaring, Batch& batch) {
  for (const auto& [klass, snapshot] : batch.classes)
    if (jni->IsSameObject(klass, declaring)) {
      batch.active_frame = snapshot.live.descriptor;
      return JVMTI_ERROR_NONE;
    }
  // A preceding structural/body update retains obsolete declaring Class
  // objects on stacks. They differ from the current JNI class reference but
  // still have self access iff descriptor and defining loader both match.
  char* signature = nullptr;
  jvmtiError result = env->GetClassSignature(declaring, &signature, nullptr);
  if (result != JVMTI_ERROR_NONE) return result;
  for (const auto& [klass, snapshot] : batch.classes) {
    if (!signature || snapshot.live.descriptor != signature) continue;
    jobject old_loader = nullptr, current_loader = nullptr;
    result = env->GetClassLoader(declaring, &old_loader);
    if (result == JVMTI_ERROR_NONE) result = env->GetClassLoader(klass, &current_loader);
    if (result == JVMTI_ERROR_NONE && jni->IsSameObject(old_loader, current_loader))
      batch.active_frame = snapshot.live.descriptor;
    jni->DeleteLocalRef(old_loader);
    jni->DeleteLocalRef(current_loader);
    if (result != JVMTI_ERROR_NONE || batch.active_frame) break;
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(signature));
  return result;
}

jvmtiError FindActiveBatchFrame(jvmtiEnv* env, JNIEnv* jni, Batch& batch) {
  jvmtiStackInfo* stacks = nullptr;
  jint threads = 0;
  jvmtiError result = env->GetAllStackTraces(1024, &stacks, &threads);
  if (result != JVMTI_ERROR_NONE) return result;
  for (jint t = 0; t < threads && !batch.active_frame && result == JVMTI_ERROR_NONE; ++t) {
    for (jint f = 0; f < stacks[t].frame_count && !batch.active_frame && result == JVMTI_ERROR_NONE; ++f) {
      jclass declaring = nullptr;
      result = env->GetMethodDeclaringClass(stacks[t].frame_buffer[f].method, &declaring);
      if (result == JVMTI_ERROR_NONE) result = MatchActiveClass(env, jni, declaring, batch);
      jni->DeleteLocalRef(declaring);
    }
  }
  for (jint t = 0; t < threads; ++t) jni->DeleteLocalRef(stacks[t].thread);
  env->Deallocate(reinterpret_cast<unsigned char*>(stacks));
  return result;
}

// A submitted definition drops an interface its class implements now. An
// unreadable definition counts as dropping one; CheckClass reads the bytes
// after any ClassFileLoadHook and asks for a retry if this guessed wrong.
bool PredictsNarrowing(JNIEnv* jni, const Batch& batch, jint count, const jvmtiClassDefinition* definitions) {
  for (jint i = 0; i < count; ++i)
    for (const auto& [klass, snapshot] : batch.classes) {
      if (!jni->IsSameObject(klass, definitions[i].klass)) continue;
      const auto size = static_cast<size_t>(definitions[i].class_byte_count);
      evolution::ClassSchema incoming;
      std::string error;
      if (!evolution::ReadClassDeclarations({definitions[i].class_bytes, size, definitions[i].class_bytes, size},
                                            &incoming, &error))
        return true;
      for (const auto& old : snapshot.live.interfaces)
        if (std::find(incoming.interfaces.begin(), incoming.interfaces.end(), old) == incoming.interfaces.end())
          return true;
    }
  return false;
}

jvmtiError RouteOnce(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions, const Commit& commit,
                     bool* subclass_loaded, bool* capture_references) {
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  transaction::Scope scope;
  transaction::Context* context = &scope.context();
  context->batch = std::make_unique<Batch>();
  Batch& batch = *BatchOf(context);
  if (!context->enclosing) {
    // lastRejection describes the latest routed transaction only.
    std::lock_guard<std::mutex> lock(process->report_mutex);
    process->last_rejection.clear();
  }
  // ART retains responsibility for arguments, caller capabilities and the
  // ClassFileLoadHook. Snapshots precede its ScopedObjectAccess transition.
  if (count <= 0 || !definitions) return InvokeStructural(env, count, definitions, context, commit);
  JNIEnv* jni = nullptr;
  if (process->runtime_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  batch.jni = jni;
  runtime_thread::Probe(jni);
  verification_redirect::Begin();
  if (interfaces) interfaces->Begin(jni, process->snapshot_env, count, definitions);
  // ART rejects a missing or unmodifiable target before any validator runs;
  // do not load types for a batch it will not consider.
  for (jint i = 0; i < count; ++i) {
    jint access = 0;
    if (definitions[i].klass &&
        process->snapshot_env->GetClassModifiers(definitions[i].klass, &access) == JVMTI_ERROR_NONE &&
        (access & evolution::kAccInterface)) {
      if (interfaces && interfaces->Available()) {
        // Public local access can update boot entry points. Request it only for
        // an interface batch, while still Native and before the loaded-class view.
        if (method_handle_lookup::Prepare(jni, process->snapshot_env)) method_handle_lookup::Enable();
        continue;
      }
      Diagnose(Signature(definitions[i].klass) + ": " + (interfaces ? interfaces->RuntimeState() : process->interface_runtime), true);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    jboolean modifiable = JNI_TRUE;
    if (!definitions[i].klass ||
        (process->snapshot_env->IsModifiableClass(definitions[i].klass, &modifiable) == JVMTI_ERROR_NONE &&
         !modifiable))
      return InvokeStructural(env, count, definitions, context, commit);
  }
  // Retention covers every submitted target even if a nested preload-time
  // transaction invalidates and clears some of the declaration snapshots.
  std::vector<jclass> ordinary_targets, interface_targets;
  std::map<jclass, size_t> ordinary_indices;
  // Loading types precedes the loaded-class view.
  jvmtiError result = JVMTI_ERROR_NONE;
  for (jint i = 0; i < count; ++i) {
    Snapshot snapshot;
    std::string error;
    const auto reject = [&](const std::string& reason) {
      Diagnose(Signature(definitions[i].klass) + ": " + reason, true);
    };
    const int read = live_schema::ReadLiveSchema(process->snapshot_env, jni, definitions[i].klass,
                                                &snapshot.live, &error);
    if (read != JVMTI_ERROR_NONE) { reject(error); return static_cast<jvmtiError>(read); }
    if (snapshot.live.access & evolution::kAccInterface) {
      interface_targets.push_back(definitions[i].klass);
    } else {
      ordinary_indices.emplace(definitions[i].klass, ordinary_targets.size());
      ordinary_targets.push_back(definitions[i].klass);
    }
    if (const RetiredKinds* earlier = FindRetiredKinds(jni, definitions[i].klass))
      snapshot.retired = earlier->methods;
    result = hierarchy_snapshot::Prepare(process->snapshot_env, jni, definitions[i], &snapshot,
                                          &batch.type_roots, &error);
    if (result != JVMTI_ERROR_NONE) { reject(error); return result; }
    if (interfaces) interfaces->Describe(definitions[i].klass, snapshot.live);
    batch.classes.emplace(definitions[i].klass, std::move(snapshot));
  }
  // An added interface links as the Class published before the batch commits.
  for (auto& [klass, snapshot] : batch.classes) {
    for (const auto& [descriptor, added] : snapshot.interface_classes)
      for (const auto& [other, ignored] : batch.classes)
        if (!jni->IsSameObject(klass, other) && jni->IsAssignableFrom(added, other))
          snapshot.hierarchy.added_interface_in_batch = descriptor;
  }
  // Preserve JBR's rejection of two moving classes in one old subtree.
  // All other new-chain dependencies are resolved by the batch allocator.
  for (auto& [klass, snapshot] : batch.classes) {
    if (!snapshot.hierarchy.new_superclass) continue;
    for (auto& [other, other_snapshot] : batch.classes) {
      if (jni->IsSameObject(klass, other)) continue;
      const bool subclass = jni->IsAssignableFrom(other, klass);
      if (subclass && other_snapshot.hierarchy.new_superclass)
        snapshot.hierarchy.new_superclass->moving_subclass_in_batch = true;
      else if (subclass)
        other_snapshot.moved_ancestor = true;
      else if (jni->IsAssignableFrom(snapshot.superclass_class, other))
        snapshot.chain_in_batch.push_back(other);
    }
  }
  // A subclass prepared from here on is missing from the snapshot and may be
  // missing from ART's recreated subclasses: fail this attempt instead.
  WatchScope watch(context, jni, count, definitions);
  LocalClasses loaded(process->snapshot_env, jni);
  const auto loaded_start = std::chrono::steady_clock::now();
  result = process->snapshot_env->GetLoadedClasses(&loaded.count, &loaded.classes);
  // Failures without a reason here are described by Route.
  if (result != JVMTI_ERROR_NONE) return result;
  batch.loaded = loaded.classes;
  batch.loaded_count = loaded.count;
  bool changes_invoke_kind = false;
  for (const auto& [klass, snapshot] : batch.classes) changes_invoke_kind |= snapshot.changes_invoke_kind;
  if (changes_invoke_kind) {
    result = FindActiveBatchFrame(process->snapshot_env, jni, batch);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  std::string interface_error;
  const uint64_t loaded_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - loaded_start).count();
  result = interfaces ?
      interfaces->Capture(loaded.count, loaded.classes, loaded_ns, &interface_error) : JVMTI_ERROR_NONE;
  if (result != JVMTI_ERROR_NONE) {
    Diagnose(interface_error, true);
    return result;
  }
  hierarchy_snapshot::LoadedIndex index;
  std::string hierarchy_error;
  result = hierarchy_snapshot::BuildIndex(process->snapshot_env, jni, loaded.count, loaded.classes,
                                           ordinary_targets, interface_targets, &index,
                                           &hierarchy_error);
  if (result != JVMTI_ERROR_NONE) { Diagnose(hierarchy_error, true); return result; }
  for (auto& [klass, snapshot] : batch.classes) {
    std::string error;
    if (snapshot.live.access & evolution::kAccInterface) {
      const std::vector<jclass>* group = interfaces ? interfaces->GroupTypes(klass) : nullptr;
      if (!group) {
        Diagnose(snapshot.live.descriptor + ": interface subtype group was not captured", true);
        return JVMTI_ERROR_INTERNAL;
      }
      result = hierarchy_snapshot::CollectInterfaceSubclasses(process->snapshot_env, jni, klass, *group,
                                                                &snapshot, &error);
      // Proxies are retained in the affected union but intentionally excluded
      // from ART's ordinary implementation rebuild. They still prevent an
      // interface from becoming a superclass-bearing class. Include every
      // loaded subtype, including not-yet-prepared ones, in that kind check.
      if (result == JVMTI_ERROR_NONE && !snapshot.hierarchy.loaded_subclasses)
        for (jclass candidate : index.affected)
          if (!jni->IsSameObject(candidate, klass) && jni->IsAssignableFrom(candidate, klass)) {
            snapshot.hierarchy.loaded_subclasses = true;
            break;
          }
    } else {
      snapshot.subtypes = &index.subtypes[ordinary_indices.at(klass)];
      result = hierarchy_snapshot::CollectSubclasses(process->snapshot_env, jni, klass,
                                                      *snapshot.subtypes, &snapshot, &error);
    }
    if (result != JVMTI_ERROR_NONE) {
      Diagnose(snapshot.live.descriptor + ": " + error, true);
      return result;
    }
  }
  // JBR marks the entire affected hierarchy as redefining, even when ART
  // keeps a body-only target's Class. Capture that identity set while Native.
  for (jclass candidate : index.affected) {
    jobject root = jni->NewGlobalRef(candidate);
    if (!root) {
      if (jni->ExceptionCheck()) jni->ExceptionClear();
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    batch.type_roots.push_back(root);
    transaction::AffectedType type{root, {}};
    jint count_interfaces = 0;
    jclass* direct = nullptr;
    result = process->snapshot_env->GetImplementedInterfaces(candidate, &count_interfaces, &direct);
    // An unprepared type has no live instances and is retained separately for
    // ART's late-subclass safety, but cannot be a FieldCopier value class yet.
    if (result == JVMTI_ERROR_CLASS_NOT_PREPARED) continue;
    if (result != JVMTI_ERROR_NONE) return result;
    for (jint i = 0; i < count_interfaces; ++i) {
      type.interfaces.push_back(Signature(direct[i]));
      jni->DeleteLocalRef(direct[i]);
    }
    process->snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(direct));
    context->affected_types.push_back(std::move(type));
  }
  // The reference capture walks every loaded class; only a batch that drops
  // an interface needs it.
  if (*capture_references || PredictsNarrowing(jni, batch, count, definitions)) {
    std::string reference_error;
    result = narrowed_references::Capture(process->snapshot_env, jni, loaded.count, loaded.classes, &reference_error);
    if (result != JVMTI_ERROR_NONE) { Diagnose(reference_error, true); return result; }
  }
  if (process->capabilities.retirement) {
    // Build the permanent roots while Native, before ART's no-failure point.
    // Root all affected loaded subclasses: each replacement ClassExt retains
    // its own obsolete chain, including native ArtMethod storage and DexCache.
    for (jclass candidate : index.affected) {
      if (member_retirement::Rooted(jni, candidate)) continue;
      jobject root = jni->NewGlobalRef(candidate);
      if (!root) {
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
      batch.retirement_roots.push_back(root);
    }
  }
  ARTDCEVM_TEST(fault_injection::SnapshotFinished(loaded_start, count, loaded.count));
  result = InvokeStructural(env, count, definitions, context, commit);
  *capture_references |= context->capture_references_on_retry;
  *subclass_loaded = watch.SubclassLoaded() || ((context->narrowing || context->protect_invocations) && watch.AnyClassPrepared()) ||
                     context->capture_references_on_retry;
  return result;
}

// Preloading covers the classes the submitted DEX names. A ClassFileLoadHook
// may name others, and another thread may load a subclass before ART pauses
// class definition. Such an attempt fails before ART's no-failure point and
// leaves the class loaded, so the next attempt includes it.
jvmtiError RouteWith(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions, const Commit& commit) {
  std::lock_guard<std::recursive_mutex> transaction_lock(process->transaction_mutex);
  constexpr int kAttempts = 3;
  // Keep this request within the routed transaction, including nested calls
  // made by a ClassFileLoadHook; one transaction cannot consume another's retry.
  bool capture_references = false;
  for (int attempt = 1;; ++attempt) {
    bool subclass_loaded = false;
    const jvmtiError result = RouteOnce(env, count, definitions, commit, &subclass_loaded, &capture_references);
    if (result == JVMTI_ERROR_NONE) return result;
    if (subclass_loaded && attempt < kAttempts) {
      process->subclass_retries.fetch_add(1);
      continue;
    }
    // Every failure names its classes, whichever check rejected it.
    bool explained;
    {
      std::lock_guard<std::mutex> lock(process->report_mutex);
      explained = !process->last_rejection.empty();
    }
    if (!explained) Diagnose(DescribeFailure(count, definitions, result), true);
    return result;
  }
}

jvmtiError CommitStructural(const void*, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  return process->structural_entry(env, count, definitions);
}

jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) noexcept try {
  return RouteWith(env, count, definitions, {CommitStructural, nullptr});
} catch (...) {
  return ArtHookExceptionError();
}

namespace {
namespace defs = abi::arm64::class_definition;
// The classes of the RetransformClasses call on this thread, while ART runs it.
struct Retransforming {
  jint count;
  const jclass* classes;
};
thread_local const Retransforming* retransforming = nullptr;
class RetransformScope {
 public:
  explicit RetransformScope(const Retransforming* call)
      : enclosing_(std::exchange(retransforming, call)) {}
  ~RetransformScope() { retransforming = enclosing_; }
  RetransformScope(const RetransformScope&) = delete;
  RetransformScope& operator=(const RetransformScope&) = delete;
 private:
  const Retransforming* enclosing_;
};

// ART's arguments to RedefineClassesDirect, committed with kStructural.
struct DirectCall {
  void* env;
  void* runtime;
  void* self;
  const void* definitions;
  void* error;
};
constexpr int kStructuralType = 0;  // RedefinitionType::kStructural.
jvmtiError CommitDirect(const void* state, jvmtiEnv*, jint, const jvmtiClassDefinition*) {
  const auto* call = static_cast<const DirectCall*>(state);
  return reinterpret_cast<Direct>(process->original_direct)(call->env, call->runtime, call->self,
                                                            call->definitions, kStructuralType, call->error);
}

bool ValidDefinition(const defs::View& view, jclass requested) {
  const auto valid_span = [](defs::ByteRef bytes) {
    return bytes.data && bytes.size >= 0x70 && bytes.size <= (size_t{1} << 30);
  };
  return view.klass == requested && view.initialized == 1 && view.redefined == 0 &&
      view.structural <= 1 && view.from_class_ext <= 1 && valid_span(view.dex) && valid_span(view.current);
}

// Same IsModified rules as ART, including the 11--12L lazy-map early return.
// Materialize only through ART's named Native service, never a signal fault
// after transform.cc has removed its temporary definition fault handler.
bool ReadDefinition(const defs::Layout& layout, const void* address, jclass requested,
                    std::vector<jvmtiClassDefinition>* out) {
  auto view = layout.read(address);
  if (!ValidDefinition(view, requested)) return false;
  if (view.dex.data == view.current.data) return true;
  bool changed = false;
  if (view.lazy) {
    if (!view.lazy_begin || view.lazy_size < 0x70 ||
        (view.dex.data == view.lazy_begin && view.dex.size > view.lazy_size) ||
        (view.current.data == view.lazy_begin && view.current.size > view.lazy_size)) return false;
    if (view.current.data == view.lazy_begin) {
      // A transformer supplied final bytes without reading its lazy input.
      // The source deliberately returns true without comparing that input.
      changed = true;
    } else {
      if (!view.can_initialize || !process->initialize_definition) return false;
      process->initialize_definition(address);
      const auto expanded = layout.read(address);
      if (!ValidDefinition(expanded, requested) || expanded.lazy ||
          expanded.dex.data != view.dex.data || expanded.dex.size != view.dex.size ||
          expanded.current.data != view.current.data || expanded.current.size != view.current.size) return false;
      view = expanded;
    }
  }
  if (!changed)
    changed = view.dex.size != view.current.size ||
        std::memcmp(view.dex.data, view.current.data, view.dex.size) != 0;
  if (!changed) return true;
  if (std::memcmp(view.dex.data, "dex\n", 4) != 0) return false;
  out->push_back({view.klass, static_cast<jint>(view.dex.size), view.dex.data});
  return true;
}

// One fixed-layout entry per requested class, in order. The original complete
// native vector remains ART-owned and is passed unchanged to every commit.
bool ReadDefinitions(const defs::Definitions& definitions, const Retransforming& call,
                     std::vector<jvmtiClassDefinition>* out) {
  const auto* layout = abi::arm64::ClassDefinitionsFor(android_get_device_api_level());
  const auto begin = reinterpret_cast<uintptr_t>(definitions.begin);
  const auto end = reinterpret_cast<uintptr_t>(definitions.end);
  const auto capacity = reinterpret_cast<uintptr_t>(definitions.capacity);
  if (!layout || call.count <= 0 || !call.classes || !begin || begin % alignof(void*) || end < begin ||
      capacity < end || end - begin != static_cast<size_t>(call.count) * layout->stride)
    return false;
  for (jint i = 0; i < call.count; ++i)
    if (!ReadDefinition(*layout, reinterpret_cast<const void*>(begin + i * layout->stride), call.classes[i], out))
      return false;
  return true;
}
}  // namespace

jvmtiError Retransform(jvmtiEnv* env, jint count, const jclass* classes) noexcept try {
  std::lock_guard<std::recursive_mutex> transaction_lock(process->transaction_mutex);
  if (!transaction::Current()) {
    std::lock_guard<std::mutex> lock(process->report_mutex);
    process->last_rejection.clear();
  }
  const Retransforming call{count, classes};
  RetransformScope scope(&call);
  const jvmtiError result = reinterpret_cast<jvmtiError (*)(jvmtiEnv*, jint, const jclass*)>(
      process->original_retransform)(env, count, classes);
  if (result != JVMTI_ERROR_NONE) {
    bool explained;
    {
      std::lock_guard<std::mutex> lock(process->report_mutex);
      explained = !process->last_rejection.empty();
    }
    if (!explained) Diagnose("RetransformClasses: ART rejected the request with " + ErrorName(result), true);
  }
  return result;
} catch (...) {
  return ArtHookExceptionError();
}

// Transformer::RetransformClasses hands its definitions here after every
// ClassFileLoadHook. Outside an engine transaction, run the standard entry's
// Native phase on those final bytes and let ART commit them structurally.
// RouteWith retries only Direct with the same immutable definitions; hooks
// have already finished and must not run again on any of those attempts.
// A definition that does not match the fixed declaration keeps ART's rules.
jvmtiError RedefineDirect(void* env, void* runtime, void* self, const void* definitions, int type,
                          void* error) noexcept try {
  const auto original = reinterpret_cast<Direct>(process->original_direct);
  const Retransforming* call = retransforming;
  RetransformScope scope(nullptr);
  if (!call || BatchOf(transaction::Current()))
    return original(env, runtime, self, definitions, type, error);
  std::vector<jvmtiClassDefinition> read;
  const auto* list = static_cast<const defs::Definitions*>(definitions);
  if (!list || !ReadDefinitions(*list, *call, &read)) {
    const jvmtiError result = original(env, runtime, self, definitions, type, error);
    if (result != JVMTI_ERROR_NONE)
      Diagnose("RetransformClasses kept ART's own rules: final definitions did not match the fixed declaration; " +
                   ErrorName(result), true);
    return result;
  }
  if (read.empty()) return original(env, runtime, self, definitions, type, error);
  const DirectCall direct{env, runtime, self, list, error};
  return RouteWith(static_cast<jvmtiEnv*>(env), static_cast<jint>(read.size()), read.data(),
                   {CommitDirect, &direct});
} catch (...) {
  return ArtHookExceptionError();
}
}  // namespace artdcevm::structural_redefine::detail
