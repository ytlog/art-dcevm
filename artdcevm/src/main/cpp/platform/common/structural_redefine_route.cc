#include "platform/common/structural_redefine_internal.h"

#include <algorithm>
#include <chrono>
#include <utility>
#include "platform/common/art_hook.h"
#include "platform/common/fault_injection.h"
#include "platform/common/runtime_thread.h"
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
  std::atomic<bool> subclass_loaded{false};
};
void JNICALL ClassPrepared(jvmtiEnv*, JNIEnv* jni, jthread, jclass klass) noexcept {
  std::lock_guard<std::mutex> lock(process->watch_mutex);
  for (Watch* watch : process->watches)
    for (jobject target : watch->targets)
      if (!jni->IsSameObject(klass, target) &&
          jni->IsAssignableFrom(klass, static_cast<jclass>(target)))
        watch->subclass_loaded = true;
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
                            transaction::Context* context) {
  const jvmtiError result = process->structural_entry(env, count, definitions);
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  if (std::string reason = interfaces ? interfaces->TakeRejection() : std::string{}; !reason.empty()) Diagnose(reason, true);
  // A rejection decided after allocation (CollectAndCreateNewInstances).
  if (std::string late = std::exchange(context->rejection, {}); !late.empty()) Diagnose(late, true);
  if (result == JVMTI_ERROR_NONE && count > 0) {
    Batch* batch = BatchOf(context);
    if (batch->retires_members) {
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
jvmtiError FindActiveBatchFrame(jvmtiEnv* env, JNIEnv* jni, Batch& batch) {
  jvmtiStackInfo* stacks = nullptr;
  jint threads = 0;
  const jvmtiError result = env->GetAllStackTraces(1024, &stacks, &threads);
  if (result != JVMTI_ERROR_NONE) return result;
  for (jint t = 0; t < threads && !batch.active_frame; ++t) {
    for (jint f = 0; f < stacks[t].frame_count && !batch.active_frame; ++f) {
      jclass declaring = nullptr;
      if (env->GetMethodDeclaringClass(stacks[t].frame_buffer[f].method, &declaring) != JVMTI_ERROR_NONE) continue;
      for (const auto& [klass, snapshot] : batch.classes)
        if (jni->IsSameObject(klass, declaring)) batch.active_frame = snapshot.live.descriptor;
      jni->DeleteLocalRef(declaring);
    }
  }
  for (jint t = 0; t < threads; ++t) jni->DeleteLocalRef(stacks[t].thread);
  env->Deallocate(reinterpret_cast<unsigned char*>(stacks));
  return JVMTI_ERROR_NONE;
}

jvmtiError RouteOnce(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions,
                     bool* subclass_loaded) {
  interface_redefinition::Backend* interfaces = InterfaceBackend();
  transaction::Scope scope;
  transaction::Context* context = &scope.context();
  context->batch = std::make_unique<Batch>();
  Batch& batch = *BatchOf(context);
  if (!context->enclosing) {
    // lastRejection describes the latest standard-entry transaction only.
    std::lock_guard<std::mutex> lock(process->report_mutex);
    process->last_rejection.clear();
  }
  // ART retains responsibility for arguments, caller capabilities and the
  // ClassFileLoadHook. Snapshots precede its ScopedObjectAccess transition.
  if (count <= 0 || !definitions) return InvokeStructural(env, count, definitions, context);
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
      if (interfaces && interfaces->Available()) continue;
      Diagnose(Signature(definitions[i].klass) + ": " + (interfaces ? interfaces->RuntimeState() : process->interface_runtime), true);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    jboolean modifiable = JNI_TRUE;
    if (!definitions[i].klass ||
        (process->snapshot_env->IsModifiableClass(definitions[i].klass, &modifiable) == JVMTI_ERROR_NONE &&
         !modifiable))
      return InvokeStructural(env, count, definitions, context);
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
  // ART recreates a target's subclasses under its new class, allocating a
  // batched subclass from its own DEX. That subclass may evolve with the
  // target unless it moves too; a supertype of the target or a class on the
  // new superclass chain would link the version published before the batch.
  for (auto& [klass, snapshot] : batch.classes) {
    if (!snapshot.hierarchy.new_superclass) continue;
    for (auto& [other, other_snapshot] : batch.classes) {
      if (jni->IsSameObject(klass, other)) continue;
      const bool subclass = jni->IsAssignableFrom(other, klass);
      if (jni->IsAssignableFrom(klass, other) || jni->IsAssignableFrom(snapshot.superclass_class, other) ||
          (subclass && other_snapshot.hierarchy.new_superclass))
        snapshot.hierarchy.new_superclass->related_in_batch = true;
      else if (subclass)
        other_snapshot.moved_ancestor = true;
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
  result = InvokeStructural(env, count, definitions, context);
  *subclass_loaded = watch.SubclassLoaded();
  return result;
}

// Preloading covers the classes the submitted DEX names. A ClassFileLoadHook
// may name others, and another thread may load a subclass before ART pauses
// class definition. Such an attempt fails before ART's no-failure point and
// leaves the class loaded, so the next attempt includes it.
jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) noexcept try {
  std::lock_guard<std::recursive_mutex> transaction_lock(process->transaction_mutex);
  constexpr int kAttempts = 3;
  for (int attempt = 1;; ++attempt) {
    bool subclass_loaded = false;
    const jvmtiError result = RouteOnce(env, count, definitions, &subclass_loaded);
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
} catch (...) {
  return ArtHookExceptionError();
}

}  // namespace artdcevm::structural_redefine::detail
