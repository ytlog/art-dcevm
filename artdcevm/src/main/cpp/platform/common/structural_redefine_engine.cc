#include "platform/common/structural_redefine_engine.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>

#include "evolution/class_schema.h"
#include "shadowhook.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/structural_policy.h"
#include "platform/common/method_retirement.h"
#include "platform/common/type_narrowing.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "support/json.h"

namespace artdcevm::structural_redefine {
namespace {
using abi::arm64::ClassRedefinitionPrefix;
using Entry = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
using Check = bool (*)(ClassRedefinitionPrefix*);
using Record = void (*)(ClassRedefinitionPrefix*);

JavaVM* runtime_vm = nullptr;
jvmtiEnv* snapshot_env = nullptr;
Entry structural_entry = nullptr;
Record record_field = nullptr;
Record record_method = nullptr;
void* original_entry = nullptr;
void* original_class = nullptr;
void* original_fields = nullptr;
void* original_methods = nullptr;
std::vector<void*> hook_tokens;
// ART takes its own redefinition lock after our Native-state snapshots.
// Keep snapshots and the transaction together so another routed batch cannot
// validate against an older definition while waiting for ART's lock.
std::recursive_mutex transaction_mutex;
std::mutex report_mutex;
bool installed = false;
std::string diagnostic = "engine not installed";
std::string last_rejection;

void Diagnose(const std::string& text, bool rejection = false) {
  {
    std::lock_guard<std::mutex> lock(report_mutex);
    (rejection ? last_rejection : diagnostic) = text;
  }
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", text.c_str());
}

struct Snapshot {
  evolution::ClassSchema live;
  structural_policy::LoadedHierarchy hierarchy;
  // The submitted definition before any ClassFileLoadHook, when readable.
  std::optional<evolution::ClassSchema> incoming;
  // Global references of hierarchy.added_interfaces, owned by the batch.
  std::map<std::string, jclass> interface_classes;
};

struct Batch {
  Batch* enclosing = nullptr;
  std::map<jclass, Snapshot> classes;
  std::map<ClassRedefinitionPrefix*, structural_policy::Decision> decisions;
  JNIEnv* jni = nullptr;
  std::vector<jobject> retirement_roots;
  std::vector<jobject> interface_roots;
  bool retires_members = false;
  ~Batch() {
    if (!jni) return;
    for (jobject root : retirement_roots) jni->DeleteGlobalRef(root);
    for (jobject root : interface_roots) jni->DeleteGlobalRef(root);
  }
};
thread_local Batch* current_batch = nullptr;

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

// A ClassFileLoadHook can start a nested transaction on this same thread.
class BatchScope {
 public:
  explicit BatchScope(Batch* batch) : previous_(current_batch) {
    batch->enclosing = previous_;
    current_batch = batch;
  }
  ~BatchScope() { current_batch = previous_; }
  BatchScope(const BatchScope&) = delete;
  BatchScope& operator=(const BatchScope&) = delete;

 private:
  Batch* previous_;
};

class LocalReference {
 public:
  explicit LocalReference(JNIEnv* jni) : jni_(jni) {}
  ~LocalReference() { if (value) jni_->DeleteLocalRef(value); }
  LocalReference(const LocalReference&) = delete;
  LocalReference& operator=(const LocalReference&) = delete;
  jobject value = nullptr;

 private:
  JNIEnv* jni_;
};

std::string_view Package(const std::string& descriptor) {
  const size_t end = descriptor.rfind('/');
  return end == std::string::npos ? std::string_view{} : std::string_view(descriptor.data(), end);
}

bool Virtual(const evolution::Member& method) {
  return !method.IsStatic() && !(method.access & evolution::kAccPrivate) &&
      method.name != "<init>" && method.name != "<clinit>";
}

// Instance methods of `klass`'s interfaces and their superinterfaces.
jvmtiError CollectInterfaceMethods(JNIEnv* jni, jclass klass, std::set<std::string>* visited,
                                   std::set<structural_policy::MemberKey>* output,
                                   std::string* error) {
  LocalClasses interfaces(snapshot_env, jni);
  jvmtiError result = snapshot_env->GetImplementedInterfaces(klass, &interfaces.count,
                                                             &interfaces.classes);
  if (result != JVMTI_ERROR_NONE) { *error = "GetImplementedInterfaces failed"; return result; }
  for (jint i = 0; i < interfaces.count; ++i) {
    char* signature = nullptr;
    result = snapshot_env->GetClassSignature(interfaces.classes[i], &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const bool first = visited->insert(signature).second;
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    if (!first) continue;
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(snapshot_env, interfaces.classes[i], &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    for (const auto& method : methods)
      if (Virtual(method)) output->insert({method.name, method.descriptor});
    result = CollectInterfaceMethods(jni, interfaces.classes[i], visited, output, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  return JVMTI_ERROR_NONE;
}

jvmtiError CollectInheritedPublic(JNIEnv* jni, jclass target, Snapshot* snapshot,
                                  std::string* error) {
  LocalReference current(jni);
  current.value = jni->GetSuperclass(target);
  while (current.value) {
    jclass klass = static_cast<jclass>(current.value);
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(snapshot_env, klass, &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    for (const auto& method : methods)
      if (Virtual(method) && (method.access & evolution::kAccPublic))
        snapshot->hierarchy.inherited_public.insert({method.name, method.descriptor});
    jclass next = jni->GetSuperclass(klass);
    jni->DeleteLocalRef(current.value);
    current.value = next;
  }
  return JVMTI_ERROR_NONE;
}

jvmtiError CollectSubclasses(JNIEnv* jni, jclass target, const LocalClasses& loaded,
                             Snapshot* snapshot, std::string* error) {
  LocalReference target_loader(jni);
  jvmtiError result = snapshot_env->GetClassLoader(target, &target_loader.value);
  if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
  for (jint n = 0; n < loaded.count; ++n) {
    jclass candidate = loaded.classes[n];
    if (jni->IsSameObject(candidate, target) || !jni->IsAssignableFrom(candidate, target)) continue;
    jint status = 0;
    result = snapshot_env->GetClassStatus(candidate, &status);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassStatus failed"; return result; }
    if (!(status & JVMTI_CLASS_STATUS_PREPARED)) continue;
    snapshot->hierarchy.loaded_subclasses = true;
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(snapshot_env, candidate, &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    char* signature = nullptr;
    result = snapshot_env->GetClassSignature(candidate, &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const bool same_package = Package(snapshot->live.descriptor) == Package(signature);
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    bool same_runtime_package = false;
    if (same_package) {
      LocalReference candidate_loader(jni);
      result = snapshot_env->GetClassLoader(candidate, &candidate_loader.value);
      if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
      same_runtime_package = jni->IsSameObject(target_loader.value, candidate_loader.value);
    }
    std::set<structural_policy::MemberKey> declared;
    for (const auto& method : methods) {
      if (!Virtual(method)) continue;
      const structural_policy::MemberKey key{method.name, method.descriptor};
      declared.insert(key);
      snapshot->hierarchy.visible.insert(key);
      if (same_runtime_package) snapshot->hierarchy.same_runtime_package.insert(key);
    }
    std::set<std::string> visited;
    std::set<structural_policy::MemberKey> interface_methods;
    result = CollectInterfaceMethods(jni, candidate, &visited, &interface_methods, error);
    if (result != JVMTI_ERROR_NONE) return result;
    for (const auto& key : interface_methods)
      if (!declared.count(key)) snapshot->hierarchy.interface_copies.insert(key);
  }
  return CollectInheritedPublic(jni, target, snapshot, error);
}

// ClassLinker::LoadSuperAndInterfaces resolves a class's interfaces through its
// defining loader. AllocateNewClassObject skips that step, and LinkClass only
// looks the interfaces up in the loader's class table, so resolve each added
// interface first: Class.forName(name, false, loader) runs ClassLinker::FindClass,
// which records the loader as an initiating loader of the class it returns.
// An interface left unresolved here is rejected by the policy in CheckClass,
// which reads the DEX after any ClassFileLoadHook.
jvmtiError ResolveAddedInterfaces(JNIEnv* jni, const jvmtiClassDefinition& definition,
                                  Batch* batch, Snapshot* snapshot, std::string* error) {
  evolution::ClassSchema incoming;
  std::string unreadable;
  const size_t size = definition.class_byte_count > 0 ? definition.class_byte_count : 0;
  const evolution::DexView view{definition.class_bytes, size, definition.class_bytes, size};
  if (!evolution::ReadClassDeclarations(view, &incoming, &unreadable)) return JVMTI_ERROR_NONE;
  snapshot->incoming = incoming;
  const auto& before = snapshot->live.interfaces;
  std::vector<std::string> added;
  for (const auto& descriptor : incoming.interfaces)
    if (std::find(before.begin(), before.end(), descriptor) == before.end() &&
        descriptor.size() > 2 && descriptor.front() == 'L' && descriptor.back() == ';')
      added.push_back(descriptor);
  if (added.empty()) return JVMTI_ERROR_NONE;
  LocalReference loader(jni);
  jvmtiError result = snapshot_env->GetClassLoader(definition.klass, &loader.value);
  if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
  LocalReference class_class(jni);
  class_class.value = jni->FindClass("java/lang/Class");
  const jmethodID for_name = class_class.value ? jni->GetStaticMethodID(
      static_cast<jclass>(class_class.value), "forName",
      "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
  if (!for_name) {
    jni->ExceptionClear();
    *error = "Class.forName unavailable";
    return JVMTI_ERROR_INTERNAL;
  }
  for (const auto& descriptor : added) {
    std::string name = descriptor.substr(1, descriptor.size() - 2);
    std::replace(name.begin(), name.end(), '/', '.');
    LocalReference binary_name(jni), resolved(jni), resolved_loader(jni);
    binary_name.value = jni->NewStringUTF(name.c_str());
    if (binary_name.value)
      resolved.value = jni->CallStaticObjectMethod(static_cast<jclass>(class_class.value), for_name,
                                                   binary_name.value, JNI_FALSE, loader.value);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    if (!resolved.value) continue;
    result = snapshot_env->GetClassLoader(static_cast<jclass>(resolved.value),
                                          &resolved_loader.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    jobject root = jni->NewGlobalRef(resolved.value);
    if (!root) {
      jni->ExceptionClear();
      *error = "cannot retain an added interface";
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    batch->interface_roots.push_back(root);
    structural_policy::ResolvedType type;
    type.same_runtime_package = Package(descriptor) == Package(snapshot->live.descriptor) &&
        jni->IsSameObject(resolved_loader.value, loader.value);
    snapshot->hierarchy.added_interfaces[descriptor] = type;
    snapshot->interface_classes[descriptor] = static_cast<jclass>(root);
  }
  return JVMTI_ERROR_NONE;
}

// The target's direct interfaces after the submitted definition (kept and
// added) and the old ones it drops.
jvmtiError SplitInterfaces(jclass target, const Snapshot& snapshot,
                           LocalClasses* current, std::vector<jclass>* removed,
                           std::vector<jclass>* after) {
  jvmtiError result = snapshot_env->GetImplementedInterfaces(target, &current->count,
                                                             &current->classes);
  if (result != JVMTI_ERROR_NONE) return result;
  const auto& now = snapshot.incoming->interfaces;
  for (jint i = 0; i < current->count; ++i) {
    char* signature = nullptr;
    result = snapshot_env->GetClassSignature(current->classes[i], &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) return result;
    const bool kept = std::find(now.begin(), now.end(), signature) != now.end();
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    (kept ? after : removed)->push_back(current->classes[i]);
  }
  for (const auto& [descriptor, klass] : snapshot.interface_classes) after->push_back(klass);
  return JVMTI_ERROR_NONE;
}

// Interface removal narrows the type of instances of the target and of its
// loaded subclasses. With every other thread suspended until ART commits
// (`world`), no location typed as a lost interface may hold such an instance.
// Marks each checked snapshot; the policy rejects unchecked removals.
jvmtiError CheckNarrowing(JNIEnv* jni, const LocalClasses& loaded, Batch* batch,
                          std::optional<type_narrowing::SuspendedWorld>* world) {
  type_narrowing::LostTypes lost;
  std::vector<Snapshot*> narrowing;
  std::string target;
  for (auto& [klass, snapshot] : batch->classes) {
    if (!snapshot.incoming || snapshot.incoming->superclass != snapshot.live.superclass) continue;
    const auto& now = snapshot.incoming->interfaces;
    if (std::all_of(snapshot.live.interfaces.begin(), snapshot.live.interfaces.end(),
                    [&](const std::string& old) {
                      return std::find(now.begin(), now.end(), old) != now.end();
                    }))
      continue;
    LocalClasses current(snapshot_env, jni);
    std::vector<jclass> removed, after;
    std::string error;
    jvmtiError result = SplitInterfaces(klass, snapshot, &current, &removed, &after);
    if (result == JVMTI_ERROR_NONE)
      result = type_narrowing::CollectLost(jni, klass, removed, after, loaded.count,
                                           loaded.classes, &lost, &error);
    if (result != JVMTI_ERROR_NONE) {
      Diagnose("type narrowing preparation failed: " + error);
      return result;
    }
    if (lost.count(snapshot.live.descriptor)) {
      narrowing.push_back(&snapshot);
      if (target.empty()) target = snapshot.live.descriptor;
    } else {
      snapshot.hierarchy.narrowing_checked = now;  // Every removed type stays inherited.
    }
  }
  if (narrowing.empty() || !type_narrowing::Available()) return JVMTI_ERROR_NONE;
  world->emplace(jni);
  std::string error;
  std::string violation;
  if ((*world)->Suspend(&error) != JVMTI_ERROR_NONE ||
      type_narrowing::FindViolation(jni, lost, &violation) != JVMTI_ERROR_NONE) {
    Diagnose(target + ": interface removal could not be checked: " +
                 (error.empty() ? "heap walk failed" : error), true);
    return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  }
  if (!violation.empty()) {
    Diagnose(target + ": interface removal would narrow a live reference: " + violation, true);
    return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
  }
  for (Snapshot* snapshot : narrowing)
    snapshot->hierarchy.narrowing_checked = snapshot->incoming->interfaces;
  // The check holds only while the world stays suspended up to ART's commit.
  method_retirement::RequireBeforeCommit(type_narrowing::CommitCheck, target);
  (*world)->Arm();
  return JVMTI_ERROR_NONE;
}

jvmtiError InvokeStructural(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  const jvmtiError result = structural_entry(env, count, definitions);
  // A rejection decided after allocation (CollectAndCreateNewInstances).
  if (std::string late = method_retirement::TakeRejection(); !late.empty()) Diagnose(late, true);
  if (result == JVMTI_ERROR_NONE && count > 0) {
    if (current_batch->retires_members) {
      method_retirement::RetainRoots(std::move(current_batch->retirement_roots));
      current_batch->retirement_roots.clear();
    }
    // A nested ClassFileLoadHook transaction may change the outer batch's
    // targets after its snapshots. Let ART's original validators handle that
    // outer batch rather than accepting a decision based on stale members.
    for (Batch* batch = current_batch->enclosing; batch; batch = batch->enclosing) {
      batch->classes.clear();
      batch->decisions.clear();
    }
  }
  return result;
}

jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  std::lock_guard<std::recursive_mutex> transaction_lock(transaction_mutex);
  Batch batch;
  BatchScope scope(&batch);
  method_retirement::TransactionScope retirement_scope;
  if (!batch.enclosing) {
    // lastRejection describes the latest standard-entry transaction only.
    std::lock_guard<std::mutex> lock(report_mutex);
    last_rejection.clear();
  }
  // ART retains responsibility for arguments, caller capabilities and the
  // ClassFileLoadHook. Snapshots precede its ScopedObjectAccess transition.
  if (count <= 0 || !definitions) return InvokeStructural(env, count, definitions);
  JNIEnv* jni = nullptr;
  if (runtime_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  batch.jni = jni;
  for (jint i = 0; i < count; ++i)
    if (!definitions[i].klass) return InvokeStructural(env, count, definitions);
  // Resolving interfaces may load classes, so it precedes the loaded-class view.
  jvmtiError result = JVMTI_ERROR_NONE;
  for (jint i = 0; i < count; ++i) {
    Snapshot snapshot;
    std::string error;
    const int read = live_schema::ReadLiveSchema(snapshot_env, jni, definitions[i].klass,
                                                &snapshot.live, &error);
    if (read != JVMTI_ERROR_NONE) { Diagnose(error); return static_cast<jvmtiError>(read); }
    result = ResolveAddedInterfaces(jni, definitions[i], &batch, &snapshot, &error);
    if (result != JVMTI_ERROR_NONE) { Diagnose(error); return result; }
    batch.classes.emplace(definitions[i].klass, std::move(snapshot));
  }
  LocalClasses loaded(snapshot_env, jni);
  result = snapshot_env->GetLoadedClasses(&loaded.count, &loaded.classes);
  if (result != JVMTI_ERROR_NONE) {
    Diagnose("GetLoadedClasses failed while preparing structural validation");
    return result;
  }
  for (auto& [klass, snapshot] : batch.classes) {
    std::string error;
    result = CollectSubclasses(jni, klass, loaded, &snapshot, &error);
    if (result != JVMTI_ERROR_NONE) { Diagnose(error); return result; }
  }
  // Declared before the transaction so suspended threads resume after it.
  std::optional<type_narrowing::SuspendedWorld> world;
  result = CheckNarrowing(jni, loaded, &batch, &world);
  if (result != JVMTI_ERROR_NONE) return result;
  if (method_retirement::Available()) {
    // Build the permanent roots while Native, before ART's no-failure point.
    // Root all affected loaded subclasses: each replacement ClassExt retains
    // its own obsolete chain, including native ArtMethod storage and DexCache.
    for (jint i = 0; i < loaded.count; ++i) {
      jclass candidate = loaded.classes[i];
      bool affected = false;
      for (jint j = 0; j < count && !affected; ++j)
        affected = jni->IsAssignableFrom(candidate, definitions[j].klass);
      if (!affected) continue;
      if (method_retirement::Rooted(jni, candidate)) continue;
      jobject root = jni->NewGlobalRef(candidate);
      if (!root) {
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
      batch.retirement_roots.push_back(root);
    }
  }
  return InvokeStructural(env, count, definitions);
}

bool CheckClass(ClassRedefinitionPrefix* self) {
  if (!current_batch) return reinterpret_cast<Check>(original_class)(self);
  current_batch->decisions.erase(self);
  const auto found = current_batch->classes.find(self->klass);
  if (found == current_batch->classes.end() || !self->dex_file || !self->dex_file->begin)
    return reinterpret_cast<Check>(original_class)(self);
  evolution::ClassSchema incoming;
  std::string error;
  if (!evolution::ReadClassDeclarations(DexViewOf(*self->dex_file), &incoming, &error)) {
    Diagnose("CheckClass DEX fallback: " + error);
    return reinterpret_cast<Check>(original_class)(self);
  }
  if (incoming.descriptor != found->second.live.descriptor)
    return reinterpret_cast<Check>(original_class)(self);
  auto& snapshot = found->second;
  structural_policy::Abilities abilities;
  abilities.member_mapping = method_retirement::VerifyTarget(self, snapshot.live);
  for (auto& [descriptor, type] : snapshot.hierarchy.added_interfaces) {
    uint32_t access = 0;
    type.access.reset();
    if (method_retirement::ClassAccess(self, snapshot.interface_classes.at(descriptor), &access))
      type.access = access;
  }
  abilities.field_retirement =
      abilities.member_mapping && method_retirement::FieldRetirementAvailable();
  abilities.instance_check = method_retirement::Available();
  auto decision = structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy,
                                              abilities);
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
  // ART's own mapping cannot commit this change; a later probe failure in
  // this transaction must reject it before ART's no-failure point.
  structural_policy::Abilities without_mapping;
  without_mapping.instance_check = abilities.instance_check;
  if (abilities.member_mapping &&
      structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy, without_mapping)
              .error != JVMTI_ERROR_NONE)
    method_retirement::RequireMemberMapping();
  if (decision.requires_no_instances)
    method_retirement::RequireNoInstances(self, incoming.descriptor);
  if (!decision.retired_methods.empty() || !decision.retired_fields.empty())
    current_batch->retires_members = true;
  current_batch->decisions.emplace(self, std::move(decision));
  return true;
}

bool CheckFields(ClassRedefinitionPrefix* self) {
  if (current_batch) {
    const auto found = current_batch->decisions.find(self);
    if (found != current_batch->decisions.end()) {
      if (found->second.fields_structural) record_field(self);
      return true;
    }
  }
  return reinterpret_cast<Check>(original_fields)(self);
}

bool CheckMethods(ClassRedefinitionPrefix* self) {
  if (current_batch) {
    const auto found = current_batch->decisions.find(self);
    if (found != current_batch->decisions.end()) {
      if (found->second.methods_structural) record_method(self);
      current_batch->decisions.erase(found);
      return true;
    }
  }
  return reinterpret_cast<Check>(original_methods)(self);
}

bool RollBack(std::string* failure) {
  bool removed_all = true;
  for (auto it = hook_tokens.rbegin(); it != hook_tokens.rend(); ++it) {
    if (!*it) continue;
    const int error = shadowhook_unhook(*it);
    if (error == 0) *it = nullptr;
    else {
      *failure += std::string("; rollback failed: ") + shadowhook_to_errmsg(error);
      removed_all = false;
    }
  }
  return removed_all;
}
}  // namespace

jvmtiError Install(JavaVM* vm, jvmtiEnv* env,
                   const method_retirement::FieldPlaceholders& placeholders) {
  {
    std::lock_guard<std::mutex> lock(report_mutex);
    if (installed) return JVMTI_ERROR_NONE;
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
  runtime_vm = vm;
  snapshot_env = env;
  structural_entry = reinterpret_cast<Entry>(addresses[1]);
  record_field = reinterpret_cast<Record>(addresses[5]);
  record_method = reinterpret_cast<Record>(addresses[6]);
  using method_retirement::Hook;
  std::vector<Hook> replacements;
  std::string retirement_note;
  method_retirement::Prepare(vm, env, &replacements, &retirement_note);
  std::string narrowing_note;
  if (method_retirement::Available()) type_narrowing::Prepare(&replacements);
  // Route last, so a routed batch can only see a complete set of validators.
  const Hook validators[] = {
      {addresses[2], reinterpret_cast<void*>(CheckClass), &original_class, names[2].name},
      {addresses[3], reinterpret_cast<void*>(CheckFields), &original_fields, names[3].name},
      {addresses[4], reinterpret_cast<void*>(CheckMethods), &original_methods, names[4].name},
      {addresses[0], reinterpret_cast<void*>(Route), &original_entry, names[0].name}};
  replacements.insert(replacements.end(), std::begin(validators), std::end(validators));
  hook_tokens.resize(replacements.size(), nullptr);
  for (size_t i = 0; i < replacements.size(); ++i) {
    const Hook& hook = replacements[i];
    hook_tokens[i] = shadowhook_hook_sym_addr(hook.target, hook.replacement, hook.original);
    if (!hook_tokens[i]) {
      const int error = shadowhook_get_errno();
      std::string failure = std::string("hook installation failed: ") + hook.name + ": " +
          shadowhook_to_errmsg(error);
      RollBack(&failure);
      Diagnose(failure);
      return JVMTI_ERROR_INTERNAL;
    }
  }
  std::string field_note;
  if (method_retirement::Available()) {
    method_retirement::ResolveFieldPlaceholders(env, placeholders, &field_note);
    type_narrowing::Activate(env, &narrowing_note);
  }
  std::lock_guard<std::mutex> lock(report_mutex);
  installed = true;
  diagnostic = !method_retirement::Available()
      ? "standard JVMTI wrapper routed to ART structural transaction; Phase 1-2 validators installed"
      : method_retirement::FieldRetirementAvailable()
          ? "ART structural transaction with method and field retirement"
          : "ART structural transaction with method retirement; fields cannot retire";
  for (const auto* note : {&retirement_note, &field_note, &narrowing_note})
    if (!note->empty()) diagnostic += "; " + *note;
  return JVMTI_ERROR_NONE;
}

std::string Capabilities() {
  std::lock_guard<std::mutex> lock(report_mutex);
  const std::string supported = installed ? "true" : "false";
  const bool methods = installed && method_retirement::Available();
  const bool fields = methods && method_retirement::FieldRetirementAvailable();
  const std::string retirement = methods ? "true" : "false";
  const std::string field_retirement = fields ? "true" : "false";
  const std::string interface_removal = methods && type_narrowing::Available() ? "true" : "false";
  return "{\"installed\":" + supported + ",\"standardRedefineRouted\":" + supported +
      ",\"memberAddition\":" + supported + ",\"memberModifierChange\":" + supported +
      ",\"classModifierChange\":" + supported + ",\"interfaceAddition\":" + retirement +
      ",\"interfaceRemoval\":" + interface_removal +
      ",\"methodDeletion\":" + retirement + ",\"methodSignatureChange\":" + retirement +
      ",\"fieldDeletion\":" + field_retirement + ",\"fieldTypeChange\":" + field_retirement +
      ",\"memberMapping\":" + QuoteJsonMutf8(method_retirement::MemberMappingState()) +
      ",\"jniRetirementGuards\":" +
          std::to_string(method_retirement::JniRetirementGuardHits()) +
      ",\"memberDeletion\":" + field_retirement + ",\"memberTypeChange\":" + field_retirement +
      ",\"hierarchyChange\":false,"
      "\"fullDcevmComplete\":false,\"diagnostic\":" + QuoteJsonMutf8(diagnostic) +
      ",\"lastRejection\":" + QuoteJsonMutf8(last_rejection) + "}";
}
}  // namespace artdcevm::structural_redefine
