#include "platform/common/structural_redefine_engine.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <utility>

#include "evolution/class_schema.h"
#include "shadowhook.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/verification_redirect.h"
#include "platform/common/fault_injection.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/hierarchy_snapshot.h"
#include "platform/common/structural_policy.h"
#include "platform/common/member_retirement.h"
#include "platform/common/member_mapping.h"
#include "platform/common/runtime_thread.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/interface_redefinition.h"
#include "platform/common/art_hook.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "support/json.h"

namespace artdcevm::structural_redefine {
namespace {
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::RedefinitionDataIter;
using Entry = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
using Check = bool (*)(ClassRedefinitionPrefix*);
using Record = void (*)(ClassRedefinitionPrefix*);

struct ProcessCapabilities {
  bool runtime_thread = false;
  bool member_mapping = false;
  bool retirement = false;
  bool field_retirement = false;
  bool superclass_change = false;
  bool instance_check = false;
  bool subclass_watch = false;
};
struct RetiredKinds {
  jobject klass;
  structural_policy::RetiredMethods methods;
};
struct Watch;
struct Process {
  JavaVM* runtime_vm = nullptr;
  jvmtiEnv* snapshot_env = nullptr;
  Entry structural_entry = nullptr;
  Record record_field = nullptr;
  Record record_method = nullptr;
  void* original_entry = nullptr;
  void* original_class = nullptr;
  void* original_fields = nullptr;
  void* original_methods = nullptr;
  void* original_instances = nullptr;
  void* original_allocate = nullptr;
  class_layout::Services layout_services{};
  // Filled only by Install. Per-transaction probes further restrict these abilities.
  ProcessCapabilities capabilities;
  std::vector<void*> hook_tokens;
  // ART takes its own redefinition lock after our Native-state snapshots.
  // Keep snapshots and the transaction together so another routed batch cannot
  // validate against an older definition while waiting for ART's lock.
  std::recursive_mutex transaction_mutex;
  std::mutex report_mutex;
  bool installed = false;
  std::string diagnostic = "engine not installed";
  std::string last_rejection;

  std::vector<RetiredKinds> retired_kinds;
  std::mutex watch_mutex;
  std::vector<Watch*> watches;
  std::atomic<uint64_t> subclass_retries{0};

  member_retirement::ProcessOwner retirement;
  interface_redefinition::ProcessOwner interfaces;
  interface_redefinition::Backend* interface_backend = nullptr;
  std::string interface_runtime = "unavailable";
};
// Serializes first construction and reports before Install creates process state.
std::mutex installation_mutex;
std::unique_ptr<Process> process;

void Diagnose(const std::string& text, bool rejection = false) {
  {
    std::lock_guard<std::mutex> lock(process->report_mutex);
    (rejection ? process->last_rejection : process->diagnostic) = text;
  }
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", text.c_str());
}

struct Snapshot : hierarchy_snapshot::Snapshot {
  // Methods retired earlier, and declarations this transaction retires.
  structural_policy::RetiredMethods retired;
  std::vector<evolution::Member> retiring;
};

// The engine's part of a transaction context.
struct Batch final : transaction::Part {
  std::map<jclass, Snapshot> classes;
  std::map<ClassRedefinitionPrefix*, structural_policy::Decision> decisions;
  JNIEnv* jni = nullptr;
  std::vector<jobject> retirement_roots;
  std::vector<jobject> type_roots;
  bool retires_members = false;
  ~Batch() override {
    if (!jni) return;
    for (jobject root : retirement_roots) jni->DeleteGlobalRef(root);
    for (jobject root : type_roots) jni->DeleteGlobalRef(root);
  }
};
Batch* BatchOf(const transaction::Context* context) {
  return transaction::PartOf<Batch>(context, &transaction::Context::batch);
}

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
    context->subclass_loaded = &watch_.subclass_loaded;
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
  if (std::string reason = process->interface_backend ? process->interface_backend->TakeRejection() : std::string{}; !reason.empty()) Diagnose(reason, true);
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

jvmtiError RouteOnce(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions,
                     bool* subclass_loaded) {
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
  if (process->interface_backend) process->interface_backend->Begin(jni, process->snapshot_env, count, definitions);
  // ART rejects a missing or unmodifiable target before any validator runs;
  // do not load types for a batch it will not consider.
  for (jint i = 0; i < count; ++i) {
    jint access = 0;
    if (definitions[i].klass &&
        process->snapshot_env->GetClassModifiers(definitions[i].klass, &access) == JVMTI_ERROR_NONE &&
        (access & evolution::kAccInterface)) {
      if (process->interface_backend && process->interface_backend->Available()) continue;
      Diagnose(Signature(definitions[i].klass) + ": " + (process->interface_backend ? process->interface_backend->RuntimeState() : process->interface_runtime), true);
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
    if (process->interface_backend) process->interface_backend->Describe(definitions[i].klass, snapshot.live);
    batch.classes.emplace(definitions[i].klass, std::move(snapshot));
  }
  // ART recreates a target's subclasses under its new class and treats each
  // target's old subclasses as its own; a superclass change cannot share that.
  for (auto& [klass, snapshot] : batch.classes) {
    if (!snapshot.hierarchy.new_superclass) continue;
    for (const auto& [other, ignored] : batch.classes)
      if (!jni->IsSameObject(klass, other) &&
          (jni->IsAssignableFrom(klass, other) || jni->IsAssignableFrom(other, klass) ||
           jni->IsAssignableFrom(snapshot.superclass_class, other)))
        snapshot.hierarchy.new_superclass->related_in_batch = true;
  }
  // A subclass prepared from here on is missing from the snapshot and may be
  // missing from ART's recreated subclasses: fail this attempt instead.
  WatchScope watch(context, jni, count, definitions);
  LocalClasses loaded(process->snapshot_env, jni);
  const auto loaded_start = std::chrono::steady_clock::now();
  result = process->snapshot_env->GetLoadedClasses(&loaded.count, &loaded.classes);
  // Failures without a reason here are described by Route.
  if (result != JVMTI_ERROR_NONE) return result;
  std::string interface_error;
  const uint64_t loaded_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - loaded_start).count();
  result = process->interface_backend ?
      process->interface_backend->Capture(loaded.count, loaded.classes, loaded_ns, &interface_error) : JVMTI_ERROR_NONE;
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
      const std::vector<jclass>* group = process->interface_backend ? process->interface_backend->GroupTypes(klass) : nullptr;
      if (!group) {
        Diagnose(snapshot.live.descriptor + ": interface subtype group was not captured", true);
        return JVMTI_ERROR_INTERNAL;
      }
      result = hierarchy_snapshot::CollectInterfaceSubclasses(process->snapshot_env, jni, klass, *group,
                                                                &snapshot, &error);
    } else {
      result = hierarchy_snapshot::CollectSubclasses(process->snapshot_env, jni, klass,
                                                      index.subtypes[ordinary_indices.at(klass)], &snapshot, &error);
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

bool CheckClass(ClassRedefinitionPrefix* self) noexcept try {
  transaction::Context* context = transaction::Current();
  Batch* batch = BatchOf(context);
  if (!batch) return reinterpret_cast<Check>(process->original_class)(self);
  batch->decisions.erase(self);
  const auto found = batch->classes.find(self->klass);
  if (found == batch->classes.end() || !self->dex_file || !self->dex_file->begin)
    return reinterpret_cast<Check>(process->original_class)(self);
  evolution::ClassSchema incoming;
  std::string error;
  if (!evolution::ReadClassDeclarations(DexViewOf(*self->dex_file), &incoming, &error)) {
    Diagnose("CheckClass DEX fallback: " + error);
    return reinterpret_cast<Check>(process->original_class)(self);
  }
  if (incoming.descriptor != found->second.live.descriptor)
    return reinterpret_cast<Check>(process->original_class)(self);
  auto& snapshot = found->second;
  structural_policy::Abilities abilities;
  const auto thread = runtime_thread::Ready();
  abilities.member_mapping = process->capabilities.member_mapping &&
      class_layout::VerifyTarget(thread, self, snapshot.live);
  for (auto& [descriptor, type] : snapshot.hierarchy.added_interfaces) {
    uint32_t access = 0;
    type.access.reset();
    if (thread && class_layout::ClassAccess(*thread, self, snapshot.interface_classes.at(descriptor), &access))
      type.access = access;
  }
  if (auto& superclass = snapshot.hierarchy.new_superclass) {
    uint32_t access = 0;
    superclass->access.reset();
    if (thread && class_layout::ClassAccess(*thread, self, snapshot.superclass_class, &access))
      superclass->access = access;
  }
  abilities.field_retirement =
      abilities.member_mapping && process->capabilities.field_retirement;
  abilities.instance_check = process->capabilities.instance_check && thread.has_value();
  abilities.superclass_change =
      abilities.member_mapping && process->capabilities.superclass_change;
  // The snapshot carries the interface group's facts, the mapping rejects new
  // default conflicts, and retirement roots cover the group: an interface
  // target may evolve its members whenever the engine owns the mapping.
  abilities.interface_members = abilities.member_mapping;
  abilities.interface_abstract_methods =
      (snapshot.live.access & evolution::kAccInterface) && abilities.member_mapping &&
      interface_dispatch::Available(*thread, self, (process->interface_backend && process->interface_backend->AbstractLinkErrorsAvailable()));
  if (!abilities.interface_abstract_methods)
    abilities.interface_abstract_unavailable_reason = interface_dispatch::State();
  auto decision = structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy,
                                              abilities, snapshot.retired);
  const bool interface_target = (process->interface_backend && process->interface_backend->IsTarget(self));
  if (interface_target) {
    if (decision.error == JVMTI_ERROR_NONE && !abilities.member_mapping) {
      decision.error = JVMTI_ERROR_NOT_AVAILABLE;
      decision.reason = "interface redefinition requires verified member mapping";
    }
    // Even a body edit must relink copied default methods in implementation classes.
    decision.methods_structural = true;
  }
  if (decision.error == JVMTI_ERROR_NONE &&
      (decision.methods_structural || decision.fields_structural) &&
      (process->interface_backend && process->interface_backend->SharesImplementationGroup(self))) {
    decision.error = JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
    decision.reason = "structural class target overlaps the interface implementation group";
  }
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
  // Hand the accepted decision to the hooks on ART's transaction. When ART's
  // own mapping cannot commit the change, a later probe failure in this
  // transaction must reject it before ART's no-failure point.
  transaction::Requirements& requirements = context->targets[self];
  requirements.descriptor = incoming.descriptor;
  requirements.member_mapping = interface_target || decision.requires_member_mapping;
  requirements.no_instances = decision.requires_no_instances;
  for (const auto& [name, descriptor] : decision.interface_obligations)
    context->interface_obligations.push_back({self, name, descriptor});
  // The policy rejects a superclass change related to another batch target,
  // so an interface group member never also changes its superclass.
  requirements.superclass = decision.superclass_changed ? snapshot.superclass_class : nullptr;
  requirements.redirect_verification =
      decision.superclass_changed || (process->interface_backend && process->interface_backend->IsAffected(self));
  if (!decision.retired_methods.empty() || !decision.retired_fields.empty())
    batch->retires_members = true;
  snapshot.retiring = decision.retired_methods;
  batch->decisions.emplace(self, std::move(decision));
  return true;
} catch (...) {
  self->driver->result = ArtHookExceptionError();
  return false;
}

bool CheckFields(ClassRedefinitionPrefix* self) noexcept {
  if (Batch* batch = BatchOf(transaction::Current())) {
    const auto found = batch->decisions.find(self);
    if (found != batch->decisions.end()) {
      if (found->second.fields_structural) process->record_field(self);
      return true;
    }
  }
  return reinterpret_cast<Check>(process->original_fields)(self);
}

bool CheckMethods(ClassRedefinitionPrefix* self) noexcept {
  if (Batch* batch = BatchOf(transaction::Current())) {
    const auto found = batch->decisions.find(self);
    if (found != batch->decisions.end()) {
      if (found->second.methods_structural) process->record_method(self);
      batch->decisions.erase(found);
      return true;
    }
  }
  return reinterpret_cast<Check>(process->original_methods)(self);
}

// ART links the target's new class under the old superclass it passes here
// ("We know the interfaces and supers didn't change"). For a superclass change,
// store the new superclass in that handle's slot: the slot belongs to the
// caller's handle scope, so it stays a GC root. Subclasses are allocated through
// the same redefinition with the target's new class as their superclass.
void* AllocateNewClassObject(ClassRedefinitionPrefix* self, abi::arm64::Handle old_class, abi::arm64::Handle super_class,
                             abi::arm64::Handle cache, uint16_t index) noexcept {
  const transaction::Context* context = transaction::Current();
  const transaction::Requirements* target = context ? context->Find(self) : nullptr;
  if (target && target->superclass && super_class.reference &&
      old_class.Get<void>() == process->layout_services.mirror_class(self)) {
    if (void* superclass = class_layout::DecodeClass(process->layout_services.mirror_class, self, target->superclass))
      *const_cast<uint32_t*>(super_class.reference) = class_layout::Reference(superclass);
  }
  using Function = void* (*)(ClassRedefinitionPrefix*, abi::arm64::Handle, abi::arm64::Handle, abi::arm64::Handle, uint16_t);
  return reinterpret_cast<Function>(process->original_allocate)(self, old_class, super_class, cache, index);
}

bool CheckInstances(const runtime_thread::VerifiedThread& thread,
                    ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) {
  auto* context = transaction::Current();
  for (const auto& [target, requirements] : context->targets) {
    if (!requirements.no_instances) continue;
    const auto* instances = data.OldInstances();
    const uint32_t reference = class_layout::Reference(process->layout_services.mirror_class(target));
    for (int32_t i = 0; instances && i < instances->length; ++i) {
      const auto* instance = class_layout::Element<abi::arm64::Object>(thread, instances, i);
      if (instance && instance->klass == reference)
        return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,
            requirements.descriptor + ": class made abstract has live instances");
    }
  }
  return true;
}
bool CollectAndCreateNewInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) noexcept try {
  auto* context = transaction::Current();
  const auto* target = context ? context->Find(self) : nullptr;
  const auto named = [&](const char* reason) {
    return (target ? target->descriptor + ": " : "") + reason;
  };
  if (ARTDCEVM_FAULT_IF(3, (process->interface_backend && process->interface_backend->IsTarget(self)) && data->Classes(true)))
    return context->Reject(self, JVMTI_ERROR_OUT_OF_MEMORY, named("injected instance allocation failure"));
  if (context && context->subclass_loaded && context->subclass_loaded->load())
    return context->Reject(self, JVMTI_ERROR_INTERNAL,
        named("a subtype of a redefined class was loaded during the transaction"));
  using Function = bool (*)(ClassRedefinitionPrefix*, RedefinitionDataIter*);
  if (!reinterpret_cast<Function>(process->original_instances)(self, data)) return false;
  // Corrupt only the transaction-private holder; cleanup roots are independent.
  ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT_IF(4, (process->interface_backend && process->interface_backend->IsTarget(self)) && data->Classes(true))) {
      fault_injection::CorruptHolder(*data);
    }
  )
  if (!context) return true;
  const auto thread = runtime_thread::Ready();
  if (thread && !CheckInstances(*thread, self, *data)) return false;
  if (!context->probed) return true;
  std::string why = thread ? "" : runtime_thread::Rejection();
  const int selected = thread ? class_layout::VerifyReplacements(*thread, self, *data, &why) : -1;
  if (selected >= 0 && member_mapping::Plan(*thread, self, *data, selected, &why)) {
    if (target && target->superclass &&
        !class_layout::SuperclassMatches(*thread, self, *data, target->superclass, selected))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
          named("the new superclass was not applied"));
    std::string rejection;
    if ((process->interface_backend && !process->interface_backend->Check(*thread, self, *data, &rejection)) ||
        !member_mapping::CheckDefaultConflicts(*data, &rejection))
      return context->Reject(self, JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED,
                             std::move(rejection));
    class_layout::Select(selected);
    if (ARTDCEVM_FAULT(22))
      return context->Reject(self, JVMTI_ERROR_OUT_OF_MEMORY,
                            named("injected failure after interface mapping plan"));
    return true;
  }
  if (selected >= 0) class_layout::Select(-1);
  class_layout::Report("rejected: " + why);
  if (!context->RequiresMemberMapping()) return true;
  __android_log_print(ANDROID_LOG_ERROR, "ART-DCEVM", "member mapping probe failed: %s", why.c_str());
  return context->Reject(self, JVMTI_ERROR_INTERNAL, {});
} catch (...) {
  self->driver->result = ArtHookExceptionError();
  return false;
}

// Named source declarations and java.lang.Class root shared by lower modules.
bool PrepareLayoutServices(JNIEnv* jni, std::string* failure) {
  using namespace symbols::structural;
  struct Required { symbols::NamedSymbol name; void** address; };
  const Required required[] = {
      {kFindDeclaredStaticField, HookSlot(&process->layout_services.find_static_field)},
      {kFindDeclaredInstanceField, HookSlot(&process->layout_services.find_declared_instance_field)},
      {kClassRedefinitionGetMirrorClass, HookSlot(&process->layout_services.mirror_class)},
      {kFindInstanceField, HookSlot(&process->layout_services.find_instance_field)}};
  for (const auto& entry : required) {
    *entry.address = symbols::Find(entry.name);
    if (!*entry.address) { *failure = std::string("missing ") + entry.name.name; return false; }
  }
  if (!jni) { *failure = "no JNIEnv for the JNI guards"; return false; }
  if (process->layout_services.class_type) return true;
  jclass local = jni->FindClass("java/lang/Class");
  process->layout_services.class_type = local ? static_cast<jclass>(jni->NewGlobalRef(local)) : nullptr;
  if (local) jni->DeleteLocalRef(local);
  if (process->layout_services.class_type) return true;
  jni->ExceptionClear();
  *failure = "exception classes unavailable";
  return false;
}

bool RollBack(std::string* failure, size_t count = SIZE_MAX) {
  bool removed_all = true;
  for (size_t i = std::min(count, process->hook_tokens.size()); i-- > 0;) {
    if (!process->hook_tokens[i]) continue;
    const int error = shadowhook_unhook(process->hook_tokens[i]);
    if (error == 0) process->hook_tokens[i] = nullptr;
    else {
      *failure += std::string("; rollback failed: ") + shadowhook_to_errmsg(error);
      removed_all = false;
    }
  }
  return removed_all;
}
}  // namespace

jvmtiError Install(JavaVM* vm, jvmtiEnv* env,
                   const member_retirement::FieldPlaceholders& placeholders) {
  {
    std::lock_guard<std::mutex> installation_lock(installation_mutex);
    if (!process) process = std::make_unique<Process>();
  }
  // Route cannot observe hooks until their final JNI table and capabilities publish.
  std::lock_guard<std::recursive_mutex> transaction_lock(process->transaction_mutex);
  {
    std::lock_guard<std::mutex> lock(process->report_mutex);
    if (process->installed) return JVMTI_ERROR_NONE;
  }
  if (!vm || !env) {
    Diagnose("JavaVM or JVMTI environment unavailable");
    return JVMTI_ERROR_INVALID_ENVIRONMENT;
  }
  jvmtiCapabilities potential{};
  jvmtiError result = env->GetPotentialCapabilities(&potential);
  if (result != JVMTI_ERROR_NONE || !potential.can_redefine_classes) {
    Diagnose("can_redefine_classes unavailable; process must be Java-debuggable at startup");
    return result == JVMTI_ERROR_NONE ? JVMTI_ERROR_MUST_POSSESS_CAPABILITY : result;
  }
  const int initialization = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
  if (initialization != 0) {
    Diagnose(std::string("ShadowHook initialization failed: ") + shadowhook_to_errmsg(initialization));
    return JVMTI_ERROR_INTERNAL;
  }
  using namespace symbols::structural;
  const symbols::NamedSymbol names[] = {kJvmtiRedefineClasses, kStructurallyRedefineClasses,
      kCheckClass, kCheckFields, kCheckMethods, kRecordNewFieldAdded, kRecordNewMethodAdded};
  std::array<void*, 7> addresses{};
  for (size_t i = 0; i < addresses.size(); ++i) {
    addresses[i] = symbols::Find(names[i]);
    if (!addresses[i]) {
      Diagnose(std::string("missing named symbol: ") + names[i].name);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
  }
  // Verify the public table without using its address as a hook target.
  // Installation still targets only the address returned by the named lookup.
  if (reinterpret_cast<void*>(env->functions->RedefineClasses) != addresses[0]) {
    Diagnose("standard JVMTI table does not use the named JvmtiFunctions::RedefineClasses wrapper");
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  std::string rollback = "previous partial installation";
  if (!RollBack(&rollback)) { Diagnose(rollback); return JVMTI_ERROR_INTERNAL; }
  process->runtime_vm = vm;
  process->snapshot_env = env;
  process->structural_entry = reinterpret_cast<Entry>(addresses[1]);
  process->record_field = reinterpret_cast<Record>(addresses[5]);
  process->record_method = reinterpret_cast<Record>(addresses[6]);
  std::vector<Hook> replacements;
  ProcessCapabilities prepared;
  runtime_thread::Prepare();
  prepared.runtime_thread = runtime_thread::Available();
  class_layout::Prepare({});
  std::string retirement_note, interface_note, failure;
  JNIEnv* jni = nullptr;
  vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6);
  std::vector<Hook> retirement_hooks;
  bool mapping_ready = PrepareLayoutServices(jni, &failure);
  if (mapping_ready && !process->retirement)
    process->retirement = member_retirement::CreateProcess();
  if (mapping_ready) mapping_ready = member_retirement::Prepare({process->layout_services.find_instance_field}, &retirement_hooks, &failure);
  if (mapping_ready) mapping_ready = member_mapping::Prepare(process->layout_services, &retirement_hooks, &failure);
  if (mapping_ready) mapping_ready = verification_redirect::Prepare(
      {},
      &retirement_hooks, &failure);
  if (mapping_ready) {
    struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
    const Required decisions[] = {
        {kCollectAndCreateNewInstances, HookAddress(CollectAndCreateNewInstances), &process->original_instances},
        {kAllocateNewClassObject, HookAddress(AllocateNewClassObject), &process->original_allocate}};
    for (const auto& decision : decisions) {
      void* address = symbols::Find(decision.symbol);
      if (!address) {
        failure = std::string("missing ") + decision.symbol.name;
        mapping_ready = false;
        break;
      }
      retirement_hooks.push_back({address, decision.replacement, decision.original, decision.symbol.name});
    }
  }
  if (mapping_ready) {
    class_layout::Prepare(process->layout_services);
    interface_dispatch::Prepare(jni, {process->layout_services.find_declared_instance_field,
                                      process->layout_services.mirror_class, process->layout_services.class_type});
    prepared.member_mapping = true;
    prepared.retirement = true;
    prepared.instance_check = true;
    prepared.superclass_change = verification_redirect::ResolvesClasses();
    replacements = std::move(retirement_hooks);
    class_layout::Report("unverified");
  } else {
    retirement_note = "method retirement unavailable: " + failure;
    // Native Thread probing never overwrites this installation failure.
    class_layout::Report("unavailable: " + failure);
  }
  if (!process->interfaces) process->interfaces = interface_redefinition::CreateProcess();
  process->interface_backend = interface_redefinition::Prepare(*process->interfaces,
      {prepared.superclass_change, prepared.runtime_thread,
      process->layout_services.mirror_class, process->layout_services.find_instance_field}, &replacements, &interface_note, &process->interface_runtime);
  verification_redirect::BindInterfaceBackend(process->interface_backend);
  const size_t retirement_hook_count = replacements.size();
  // Route last, so a routed batch can only see a complete set of validators.
  const Hook validators[] = {
      {addresses[2], reinterpret_cast<void*>(CheckClass), &process->original_class, names[2].name},
      {addresses[3], reinterpret_cast<void*>(CheckFields), &process->original_fields, names[3].name},
      {addresses[4], reinterpret_cast<void*>(CheckMethods), &process->original_methods, names[4].name},
      {addresses[0], reinterpret_cast<void*>(Route), &process->original_entry, names[0].name}};
  replacements.insert(replacements.end(), std::begin(validators), std::end(validators));
  process->hook_tokens.resize(replacements.size(), nullptr);
  for (size_t i = 0; i < replacements.size(); ++i) {
    const Hook& hook = replacements[i];
    process->hook_tokens[i] = shadowhook_hook_sym_addr(hook.target, hook.replacement, hook.original);
    if (!process->hook_tokens[i]) {
      const int error = shadowhook_get_errno();
      std::string failure = std::string("hook installation failed: ") + hook.name + ": " +
          shadowhook_to_errmsg(error);
      RollBack(&failure);
      Diagnose(failure);
      return JVMTI_ERROR_INTERNAL;
    }
  }
  // This process override cannot be undone; no later inline hook may fail.
  if (prepared.retirement && !member_retirement::InstallJniGuards(vm, env, &failure)) {
    std::string reason = "JNI guard installation failed: " + failure;
    if (!RollBack(&reason, retirement_hook_count)) {
      RollBack(&reason);
      Diagnose(reason);
      return JVMTI_ERROR_INTERNAL;
    }
    // Preserve the existing optional-retirement failure semantics: the base
    // validators/route stay installed, and additions/body edits remain usable.
    prepared = {};
    prepared.runtime_thread = runtime_thread::Available();
    retirement_note = "method retirement unavailable: " + failure;
    class_layout::Prepare({});
    class_layout::Report("unavailable: " + failure);
    process->interface_backend = interface_redefinition::Prepare(*process->interfaces,
        {false, prepared.runtime_thread,
        process->layout_services.mirror_class, process->layout_services.find_instance_field},
        &replacements, &interface_note, &process->interface_runtime);
    verification_redirect::BindInterfaceBackend(process->interface_backend);
  }
  // The engine's own environment carries no other events.
  jvmtiEventCallbacks callbacks{};
  callbacks.ClassPrepare = ClassPrepared;
  prepared.subclass_watch = prepared.instance_check &&
      env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE;
  std::string field_note;
  if (prepared.retirement)
    member_retirement::ResolveFieldPlaceholders(env, placeholders, &field_note);
  std::lock_guard<std::mutex> lock(process->report_mutex);
  prepared.field_retirement = prepared.retirement && member_retirement::FieldRetirementAvailable();
  process->capabilities = prepared;
  process->installed = true;
  process->diagnostic = !process->capabilities.retirement
      ? "ART structural transaction with additions and modifier changes; members cannot retire"
      : process->capabilities.field_retirement
          ? "ART structural transaction with method and field retirement"
          : "ART structural transaction with method retirement; fields cannot retire";
  for (const auto* note : {&retirement_note, &field_note, &interface_note})
    if (!note->empty()) process->diagnostic += "; " + *note;
  return JVMTI_ERROR_NONE;
}

std::string Capabilities() {
  Process* selected;
  {
    std::lock_guard<std::mutex> installation_lock(installation_mutex);
    selected = process.get();
  }
  Process empty;
  Process& state = selected ? *selected : empty;
  std::lock_guard<std::mutex> lock(state.report_mutex);
  const std::string supported = state.installed ? "true" : "false";
  const bool methods = state.installed && state.capabilities.retirement;
  const bool fields = methods && state.capabilities.field_retirement;
  const std::string retirement = methods ? "true" : "false";
  const std::string field_retirement = fields ? "true" : "false";
  const auto interface_timing = state.interface_backend ? state.interface_backend->LastTiming() : interface_redefinition::Timing{};
  // Both capability fields describe the same probe snapshot, including disablement.
  const std::string interface_state = state.interface_backend ? state.interface_backend->RuntimeState() : state.interface_runtime;
  const std::string dispatch_state = interface_dispatch::State();
  return "{\"installed\":" + supported + ",\"standardRedefineRouted\":" + supported +
      ",\"memberAddition\":" + supported + ",\"memberModifierChange\":" + supported +
      ",\"classModifierChange\":" + supported + ",\"interfaceAddition\":" + retirement + ",\"superclassWidening\":" +
          (methods && state.capabilities.superclass_change ? "true" : "false") +
      ",\"methodDeletion\":" + retirement + ",\"methodSignatureChange\":" + retirement +
      ",\"fieldDeletion\":" + field_retirement + ",\"fieldTypeChange\":" + field_retirement +
      ",\"memberMapping\":" + QuoteJsonMutf8(class_layout::State()) +
      ",\"jniRetirementGuards\":" +
          std::to_string(member_retirement::JniRetirementGuardHits()) +
      ",\"subclassLoadRetries\":" + std::to_string(state.subclass_retries.load()) +
      ",\"interfaceRedefinition\":" + (state.installed && interface_state.rfind("verified: ", 0) == 0 ? "true" : "false") +
      ",\"interfaceRuntime\":" + QuoteJsonMutf8(interface_state) +
      ",\"interfaceAbstractMethodAddition\":" +
          (state.installed && interface_state.rfind("verified: ", 0) == 0 &&
           dispatch_state.rfind("verified: ", 0) == 0 && (state.interface_backend && state.interface_backend->AbstractLinkErrorsAvailable())
               ? "true" : "false") +
      ",\"interfaceAbstractDispatch\":" + QuoteJsonMutf8(dispatch_state) +
      ",\"interfaceSnapshotTypes\":" + std::to_string(interface_timing.types) +
      ",\"interfaceSnapshotNanos\":" + std::to_string(interface_timing.snapshot_ns) +
      ",\"interfaceAllocationNanos\":" + std::to_string(interface_timing.allocation_ns) +
      ",\"memberDeletion\":" + field_retirement + ",\"memberTypeChange\":" + field_retirement +
      ",\"hierarchyChange\":false,"
      "\"fullDcevmComplete\":false,\"diagnostic\":" + QuoteJsonMutf8(state.diagnostic) +
      ",\"lastRejection\":" + QuoteJsonMutf8(state.last_rejection) + "}";
}
}  // namespace artdcevm::structural_redefine
