#include "platform/common/structural_redefine_engine.h"

#include <android/log.h>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>

#include "evolution/class_schema.h"
#include "shadowhook.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/structural_policy.h"
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
std::array<void*, 4> hook_tokens{};
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
  structural_policy::SubclassMethods subclass_methods;
};

struct Batch {
  Batch* enclosing = nullptr;
  std::map<jclass, Snapshot> classes;
  std::map<ClassRedefinitionPrefix*, structural_policy::Decision> decisions;
};
thread_local Batch* current_batch = nullptr;

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

class LoadedClasses {
 public:
  LoadedClasses(jvmtiEnv* env, JNIEnv* jni) : env_(env), jni_(jni) {}
  ~LoadedClasses() {
    if (!classes) return;
    for (jint i = 0; i < count; ++i) jni_->DeleteLocalRef(classes[i]);
    env_->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  LoadedClasses(const LoadedClasses&) = delete;
  LoadedClasses& operator=(const LoadedClasses&) = delete;
  jint count = 0;
  jclass* classes = nullptr;

 private:
  jvmtiEnv* env_;
  JNIEnv* jni_;
};

std::string_view Package(const std::string& descriptor) {
  const size_t end = descriptor.rfind('/');
  return end == std::string::npos ? std::string_view{} : std::string_view(descriptor.data(), end);
}

jvmtiError CollectSubclassMethods(JNIEnv* jni, jclass target, const LoadedClasses& loaded,
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
    for (const auto& method : methods) {
      if (method.IsStatic() || (method.access & evolution::kAccPrivate) ||
          method.name == "<init>" || method.name == "<clinit>") continue;
      const structural_policy::MemberKey key{method.name, method.descriptor};
      snapshot->subclass_methods.visible.insert(key);
      if (same_runtime_package) snapshot->subclass_methods.same_runtime_package.insert(key);
    }
  }
  return JVMTI_ERROR_NONE;
}

jvmtiError InvokeStructural(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  const jvmtiError result = structural_entry(env, count, definitions);
  if (result == JVMTI_ERROR_NONE && count > 0) {
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
  LoadedClasses loaded(snapshot_env, jni);
  jvmtiError result = snapshot_env->GetLoadedClasses(&loaded.count, &loaded.classes);
  if (result != JVMTI_ERROR_NONE) {
    Diagnose("GetLoadedClasses failed while preparing structural validation");
    return result;
  }
  for (jint i = 0; i < count; ++i) {
    if (!definitions[i].klass) return InvokeStructural(env, count, definitions);
    Snapshot snapshot;
    std::string error;
    const int read = live_schema::ReadLiveSchema(snapshot_env, jni, definitions[i].klass,
                                                &snapshot.live, &error);
    if (read != JVMTI_ERROR_NONE) { Diagnose(error); return static_cast<jvmtiError>(read); }
    result = CollectSubclassMethods(jni, definitions[i].klass, loaded, &snapshot, &error);
    if (result != JVMTI_ERROR_NONE) { Diagnose(error); return result; }
    batch.classes.emplace(definitions[i].klass, std::move(snapshot));
  }
  return InvokeStructural(env, count, definitions);
}

bool CheckClass(ClassRedefinitionPrefix* self) {
  if (!current_batch) return reinterpret_cast<Check>(original_class)(self);
  current_batch->decisions.erase(self);
  const auto found = current_batch->classes.find(self->klass);
  if (found == current_batch->classes.end() || !self->dex_file || !self->dex_file->begin)
    return reinterpret_cast<Check>(original_class)(self);
  const auto& dex = *self->dex_file;
  uint32_t file_size = 0;
  std::memcpy(&file_size, dex.begin + abi::arm64::kDexHeaderFileSizeOffset, sizeof(file_size));
  evolution::ClassSchema incoming;
  std::string error;
  if (!evolution::ReadClassDeclarations({dex.begin, file_size, dex.data_begin, dex.data_size},
                                       &incoming, &error)) {
    Diagnose("CheckClass DEX fallback: " + error);
    return reinterpret_cast<Check>(original_class)(self);
  }
  if (incoming.descriptor != found->second.live.descriptor)
    return reinterpret_cast<Check>(original_class)(self);
  auto decision = structural_policy::Evaluate(found->second.live, incoming,
                                              found->second.subclass_methods);
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
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

jvmtiError Install(JavaVM* vm, jvmtiEnv* env) {
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
  struct Hook { void* target; void* replacement; void** original; const char* name; };
  // Route last, so a routed batch can only see a complete set of validators.
  const Hook replacements[] = {
      {addresses[2], reinterpret_cast<void*>(CheckClass), &original_class, names[2].name},
      {addresses[3], reinterpret_cast<void*>(CheckFields), &original_fields, names[3].name},
      {addresses[4], reinterpret_cast<void*>(CheckMethods), &original_methods, names[4].name},
      {addresses[0], reinterpret_cast<void*>(Route), &original_entry, names[0].name}};
  for (size_t i = 0; i < hook_tokens.size(); ++i) {
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
  std::lock_guard<std::mutex> lock(report_mutex);
  installed = true;
  diagnostic = "standard JVMTI wrapper routed to ART structural transaction; Phase 1-2 validators installed";
  return JVMTI_ERROR_NONE;
}

std::string Capabilities() {
  std::lock_guard<std::mutex> lock(report_mutex);
  const std::string supported = installed ? "true" : "false";
  return "{\"installed\":" + supported + ",\"standardRedefineRouted\":" + supported +
      ",\"memberAddition\":" + supported + ",\"memberModifierChange\":" + supported +
      ",\"memberDeletion\":false,\"memberTypeChange\":false,\"hierarchyChange\":false,"
      "\"fullDcevmComplete\":false,\"diagnostic\":" + QuoteJsonMutf8(diagnostic) +
      ",\"lastRejection\":" + QuoteJsonMutf8(last_rejection) + "}";
}
}  // namespace artdcevm::structural_redefine
