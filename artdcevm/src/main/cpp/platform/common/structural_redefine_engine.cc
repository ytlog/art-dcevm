#include "platform/common/structural_redefine_engine.h"

#include <android/log.h>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "evolution/class_schema.h"
#include "hooks/shadow_hook.h"
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
Entry structural_entry = nullptr;
void* original_entry = nullptr;
void* original_class = nullptr;
void* original_fields = nullptr;
void* original_methods = nullptr;
Record record_field = nullptr;
Record record_method = nullptr;
std::mutex report_mutex;
bool installed = false;
std::string diagnostic = "engine not installed";
std::string last_rejection;

void Diagnose(const std::string& text, bool rejection = false) {
  std::lock_guard<std::mutex> lock(report_mutex);
  (rejection ? last_rejection : diagnostic) = text;
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", text.c_str());
}

struct Snapshot {
  evolution::ClassSchema live;
  std::set<structural_policy::MemberKey> subclass_methods;
};
struct Batch {
  std::map<jclass, Snapshot> classes;
  std::map<ClassRedefinitionPrefix*, structural_policy::Decision> decisions;
};
thread_local Batch* current_batch = nullptr;

// Restore the enclosing transaction even if a ClassFileLoadHook reenters.
class BatchScope {
 public:
  explicit BatchScope(Batch* batch) : previous_(current_batch) { current_batch = batch; }
  ~BatchScope() { current_batch = previous_; }
 private:
  Batch* previous_;
};

class LoadedClasses {
 public:
  LoadedClasses(jvmtiEnv* env, JNIEnv* jni) : env_(env), jni_(jni) {}
  ~LoadedClasses() {
    if (!classes) return;
    for (jint i = 0; i < count; ++i) jni_->DeleteLocalRef(classes[i]);
    env_->Deallocate(reinterpret_cast<unsigned char*>(classes));
  }
  jint count = 0;
  jclass* classes = nullptr;
 private:
  jvmtiEnv* env_;
  JNIEnv* jni_;
};

jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  Batch batch;
  BatchScope scope(&batch);
  // The public-table wrapper normally enforces this before calling Redefiner.
  // Preserve the caller's capability check before any schema work.
  jvmtiCapabilities capabilities{};
  jvmtiError result = env->GetCapabilities(&capabilities);
  if (result != JVMTI_ERROR_NONE) return result;
  if (!capabilities.can_redefine_classes) return JVMTI_ERROR_MUST_POSSESS_CAPABILITY;
  // Let ART validate empty batches and invalid arguments itself.
  if (count <= 0 || !definitions) return structural_entry(env, count, definitions);
  JNIEnv* jni = nullptr;
  if (runtime_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  LoadedClasses loaded(env, jni);
  result = env->GetLoadedClasses(&loaded.count, &loaded.classes);
  if (result != JVMTI_ERROR_NONE) return result;
  for (jint i = 0; i < count; ++i) {
    if (!definitions[i].klass) return structural_entry(env, count, definitions);
    Snapshot snapshot;
    std::string error;
    int read = live_schema::ReadLiveSchema(env, jni, definitions[i].klass, &snapshot.live, &error);
    if (read) {
      Diagnose(error);
      return static_cast<jvmtiError>(read);
    }
    for (jint n = 0; n < loaded.count; ++n) {
      jclass candidate = loaded.classes[n];
      if (jni->IsSameObject(candidate, definitions[i].klass) ||
          !jni->IsAssignableFrom(candidate, definitions[i].klass)) continue;
      jint status = 0;
      result = env->GetClassStatus(candidate, &status);
      if (result != JVMTI_ERROR_NONE) return result;
      if (!(status & JVMTI_CLASS_STATUS_PREPARED)) continue;
      std::vector<evolution::Member> methods;
      read = live_schema::ReadLiveMethods(env, candidate, &methods, &error);
      if (read) {
        Diagnose(error);
        return static_cast<jvmtiError>(read);
      }
      for (const auto& method : methods) {
        if (!method.IsStatic() && !(method.access & evolution::kAccPrivate) &&
            method.name != "<init>" && method.name != "<clinit>")
          snapshot.subclass_methods.emplace(method.name, method.descriptor);
      }
    }
    batch.classes.emplace(definitions[i].klass, std::move(snapshot));
  }
  return structural_entry(env, count, definitions);
}

bool CheckClass(ClassRedefinitionPrefix* self) {
  if (!current_batch) return reinterpret_cast<Check>(original_class)(self);
  current_batch->decisions.erase(self);
  const auto found = current_batch->classes.find(self->klass);
  if (found == current_batch->classes.end() || !self->dex_file || !self->dex_file->begin) {
    return reinterpret_cast<Check>(original_class)(self);
  }
  const auto& dex = *self->dex_file;
  uint32_t file_size = 0;
  std::memcpy(&file_size, dex.begin + abi::arm64::kDexHeaderFileSizeOffset, sizeof(file_size));
  evolution::ClassSchema incoming;
  std::string error;
  if (!evolution::ReadClassDeclarations({dex.begin, file_size, dex.data_begin, dex.data_size},
                                       &incoming, &error) ||
      incoming.descriptor != found->second.live.descriptor) {
    Diagnose("CheckClass DEX fallback: " + error + "; expected " + found->second.live.descriptor +
             "; read " + incoming.descriptor);
    return reinterpret_cast<Check>(original_class)(self);
  }
  auto decision = structural_policy::Evaluate(found->second.live, incoming,
                                              found->second.subclass_methods);
  if (decision.error != JVMTI_ERROR_NONE) {
    self->driver->result = decision.error;
    Diagnose(incoming.descriptor + ": " + decision.reason, true);
    return false;
  }
  // Phase 2 changes no class metadata. Preserve ART's class lock check and
  // ClassDef flags comparison, including nested-class flags that the public
  // JVMTI view cannot compare directly. Member validation is separate.
  if (!reinterpret_cast<Check>(original_class)(self)) {
    Diagnose(incoming.descriptor + ": ART class metadata or lock validation failed", true);
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
}  // namespace

jvmtiError Install(JavaVM* vm, jvmtiEnv* env) {
  {
    std::lock_guard<std::mutex> lock(report_mutex);
    if (installed) return JVMTI_ERROR_NONE;
  }
  if (!vm || !env) return JVMTI_ERROR_INVALID_ENVIRONMENT;
  jvmtiCapabilities potential{};
  jvmtiError result = env->GetPotentialCapabilities(&potential);
  if (result != JVMTI_ERROR_NONE || !potential.can_redefine_classes) {
    Diagnose("can_redefine_classes unavailable; process must be Java-debuggable at startup");
    return result == JVMTI_ERROR_NONE ? JVMTI_ERROR_MUST_POSSESS_CAPABILITY : result;
  }
  std::string error;
  if (!hooks::Initialize(&error)) {
    Diagnose("ShadowHook initialization failed: " + error);
    return JVMTI_ERROR_INTERNAL;
  }
  using namespace symbols::structural;
  const symbols::NamedSymbol names[] = {kJvmtiRedefineClasses, kStructurallyRedefineClasses,
      kCheckClass, kCheckFields, kCheckMethods, kRecordNewFieldAdded, kRecordNewMethodAdded};
  void* addresses[7]{};
  for (size_t i = 0; i < 7; ++i) {
    addresses[i] = symbols::Find(names[i]);
    if (!addresses[i]) {
      Diagnose(std::string("missing named symbol: ") + names[i].name);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
  }
  runtime_vm = vm;
  if (reinterpret_cast<void*>(env->functions->RedefineClasses) != addresses[0]) {
    Diagnose("named standard entry does not match the public JVMTI table");
    return JVMTI_ERROR_NOT_AVAILABLE;
  }
  structural_entry = reinterpret_cast<Entry>(addresses[1]);
  record_field = reinterpret_cast<Record>(addresses[5]);
  record_method = reinterpret_cast<Record>(addresses[6]);
  // Install the route last: no routed transaction can see partial validators.
  struct Hook { void* target; void* replacement; void** original; };
  const Hook hooks_to_install[] = {
      {addresses[2], reinterpret_cast<void*>(CheckClass), &original_class},
      {addresses[3], reinterpret_cast<void*>(CheckFields), &original_fields},
      {addresses[4], reinterpret_cast<void*>(CheckMethods), &original_methods},
      {addresses[0], reinterpret_cast<void*>(Route), &original_entry}};
  std::vector<void*> tokens;
  for (const auto& hook : hooks_to_install) {
    void* token = hooks::Install(hook.target, hook.replacement, hook.original, &error);
    if (!token) {
      std::string failure = "hook installation failed: " + error;
      for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (!hooks::Remove(*it, &error)) failure += "; rollback failed: " + error;
      }
      Diagnose(failure);
      return JVMTI_ERROR_INTERNAL;
    }
    tokens.push_back(token);
  }
  std::lock_guard<std::mutex> lock(report_mutex);
  installed = true;
  diagnostic = "ART structural transaction routed; Phase 1-2 validators installed";
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
