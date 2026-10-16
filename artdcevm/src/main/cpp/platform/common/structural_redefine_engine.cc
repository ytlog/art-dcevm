#include "platform/common/structural_redefine_engine.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>

#include "evolution/class_schema.h"
#include "shadowhook.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/class_layout.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/structural_policy.h"
#include "platform/common/member_retirement.h"
#include "platform/common/member_mapping.h"
#include "platform/common/interface_dispatch.h"
#include "platform/common/interface_redefinition.h"
#include "platform/common/art_hook.h"
#include "platform/common/transaction_context.h"
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
  // Methods the class retired in earlier transactions, and those this one retires.
  structural_policy::RetiredMethods retired;
  std::vector<evolution::Member> retiring;
  // Global references of hierarchy.added_interfaces and new_superclass, owned by the batch.
  std::map<std::string, jclass> interface_classes;
  jclass superclass_class = nullptr;
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
struct RetiredKinds {
  jobject klass;
  structural_policy::RetiredMethods methods;
};
std::vector<RetiredKinds> retired_kinds;

RetiredKinds* FindRetiredKinds(JNIEnv* jni, jclass klass) {
  for (RetiredKinds& entry : retired_kinds)
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
    entry = &retired_kinds.emplace_back(RetiredKinds{root, {}});
  }
  for (const auto& method : methods) entry->methods[{method.name, method.descriptor}] = method.access;
}

// Targets of a routed transaction whose subclass the JVMTI ClassPrepare event
// reports after the engine's snapshot, on any thread.
struct Watch {
  std::vector<jobject> targets;  // Global references.
  std::atomic<bool> subclass_loaded{false};
};
std::mutex watch_mutex;
std::vector<Watch*> watches;
bool watch_available = false;
std::atomic<uint64_t> subclass_retries{0};

void JNICALL ClassPrepared(jvmtiEnv*, JNIEnv* jni, jthread, jclass klass) noexcept {
  std::lock_guard<std::mutex> lock(watch_mutex);
  for (Watch* watch : watches)
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
    if (!watch_available) return;
    for (jint i = 0; i < count; ++i)
      if (jobject target = jni->NewGlobalRef(definitions[i].klass)) watch_.targets.push_back(target);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    bool first;
    {
      std::lock_guard<std::mutex> lock(watch_mutex);
      first = watches.empty();
      watches.push_back(&watch_);
    }
    if (first) snapshot_env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);
    context->subclass_loaded = &watch_.subclass_loaded;
  }
  ~WatchScope() {
    if (!watch_available) return;
    bool last;
    {
      std::lock_guard<std::mutex> lock(watch_mutex);
      watches.erase(std::find(watches.begin(), watches.end(), &watch_));
      last = watches.empty();
    }
    if (last) snapshot_env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);
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

std::string Signature(jclass klass) {
  char* signature = nullptr;
  if (!klass || snapshot_env->GetClassSignature(klass, &signature, nullptr) != JVMTI_ERROR_NONE)
    return "<invalid class>";
  std::string result = signature;
  snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
  return result;
}

std::string ErrorName(jvmtiError error) {
  std::string name = "JVMTI_ERROR_" + std::to_string(error);
  char* text = nullptr;
  if (snapshot_env->GetErrorName(error, &text) == JVMTI_ERROR_NONE) {
    name = text;
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(text));
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
      if (snapshot_env->IsModifiableClass(klass, &flag) == JVMTI_ERROR_NONE && !flag)
        return signature + ": ART reports the class as unmodifiable; " + ErrorName(result);
    }
    classes += (classes.empty() ? "" : ", ") + signature;
  }
  return (classes.empty() ? "" : classes + ": ") + "ART rejected the redefinition with " +
      ErrorName(result) + "; its reason follows \"FAILURE TO REDEFINE\" in the log";
}

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

// Virtual methods an interface target inherits from its superinterfaces and
// java.lang.Object (LoadedHierarchy::inherited_interface).
jvmtiError CollectInheritedInterface(JNIEnv* jni, jclass target, Snapshot* snapshot,
                                     std::string* error) {
  auto& inherited = snapshot->hierarchy.inherited_interface;
  std::set<std::string> visited;
  jvmtiError result = CollectInterfaceMethods(jni, target, &visited, &inherited, error);
  if (result != JVMTI_ERROR_NONE) return result;
  LocalReference object(jni);
  object.value = jni->FindClass("java/lang/Object");
  if (!object.value) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = "java.lang.Object unavailable";
    return JVMTI_ERROR_INTERNAL;
  }
  std::vector<evolution::Member> methods;
  const int read = live_schema::ReadLiveMethods(snapshot_env, static_cast<jclass>(object.value),
                                                &methods, error);
  if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
  for (const auto& method : methods)
    if (Virtual(method) && (method.access & evolution::kAccPublic))
      inherited.insert({method.name, method.descriptor});
  return JVMTI_ERROR_NONE;
}

// Static and non-public instance methods of `methods`, declared by
// `descriptor` (LoadedHierarchy::subtype_shadows). The first declarer names
// the shadow in a rejection.
void AddShadows(const std::vector<evolution::Member>& methods, const std::string& descriptor,
                Snapshot* snapshot) {
  for (const auto& method : methods)
    if (method.name != "<init>" && method.name != "<clinit>" &&
        (method.IsStatic() || !(method.access & evolution::kAccPublic)))
      snapshot->hierarchy.subtype_shadows.emplace(
          structural_policy::MemberKey{method.name, method.descriptor}, descriptor);
}

// Shadows declared by the superclasses of an interface target's loaded
// implementation, up to the first superclass that is itself a subtype (the
// caller visits that one). Boot classes have unique descriptors, so each is
// read once per transaction; other loaders may reuse a name.
jvmtiError CollectSuperclassShadows(JNIEnv* jni, jclass target, jclass implementation,
                                    std::set<std::string>* boot_visited, Snapshot* snapshot,
                                    std::string* error) {
  LocalReference current(jni);
  current.value = jni->GetSuperclass(implementation);
  while (current.value) {
    jclass klass = static_cast<jclass>(current.value);
    if (jni->IsAssignableFrom(klass, target)) break;
    LocalReference loader(jni);
    jvmtiError result = snapshot_env->GetClassLoader(klass, &loader.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    char* signature = nullptr;
    result = snapshot_env->GetClassSignature(klass, &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const std::string descriptor = signature;
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    if (loader.value || boot_visited->insert(descriptor).second) {
      std::vector<evolution::Member> methods;
      const int read = live_schema::ReadLiveMethods(snapshot_env, klass, &methods, error);
      if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
      AddShadows(methods, descriptor, snapshot);
    }
    jclass next = jni->GetSuperclass(klass);
    jni->DeleteLocalRef(current.value);
    current.value = next;
  }
  return JVMTI_ERROR_NONE;
}

// `candidates` are the loaded classes, or for an interface target the types of
// the group that rebuilds it, so the policy sees exactly what ART relinks.
jvmtiError CollectSubclasses(JNIEnv* jni, jclass target, const jclass* candidates, jint count,
                             Snapshot* snapshot, std::string* error) {
  const bool interface_target = snapshot->live.access & evolution::kAccInterface;
  std::set<std::string> boot_visited;
  LocalReference target_loader(jni);
  jvmtiError result = snapshot_env->GetClassLoader(target, &target_loader.value);
  if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
  for (jint n = 0; n < count; ++n) {
    jclass candidate = candidates[n];
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
    const std::string descriptor = signature;
    snapshot_env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    const bool same_package = Package(snapshot->live.descriptor) == Package(descriptor);
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
    if (!interface_target) continue;
    AddShadows(methods, descriptor, snapshot);
    jboolean is_interface = JNI_FALSE;
    result = snapshot_env->IsInterface(candidate, &is_interface);
    if (result != JVMTI_ERROR_NONE) { *error = "IsInterface failed"; return result; }
    if (is_interface) continue;
    result = CollectSuperclassShadows(jni, target, candidate, &boot_visited, snapshot, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  if (interface_target) return CollectInheritedInterface(jni, target, snapshot, error);
  return CollectInheritedPublic(jni, target, snapshot, error);
}

// Class.forName(name, false, loader) through a target's defining loader. It
// runs ClassLinker::FindClass, which records the loader as an initiating loader
// of the class it returns, and loads without initializing.
class TypeResolver {
 public:
  TypeResolver(JNIEnv* jni, jclass target) : jni_(jni), target_(target), loader_(jni),
                                             class_class_(jni) {}
  jvmtiError Init(std::string* error) {
    jvmtiError result = snapshot_env->GetClassLoader(target_, &loader_.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    class_class_.value = jni_->FindClass("java/lang/Class");
    for_name_ = class_class_.value ? jni_->GetStaticMethodID(
        static_cast<jclass>(class_class_.value), "forName",
        "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
    if (for_name_) return JVMTI_ERROR_NONE;
    jni_->ExceptionClear();
    *error = "Class.forName unavailable";
    return JVMTI_ERROR_INTERNAL;
  }
  // A local reference, or null when the loader cannot load `descriptor`.
  jobject Load(const std::string& descriptor) {
    if (descriptor.size() <= 2 || descriptor.front() != 'L' || descriptor.back() != ';')
      return nullptr;
    std::string name = descriptor.substr(1, descriptor.size() - 2);
    std::replace(name.begin(), name.end(), '/', '.');
    LocalReference binary_name(jni_);
    binary_name.value = jni_->NewStringUTF(name.c_str());
    jobject loaded = binary_name.value
        ? jni_->CallStaticObjectMethod(static_cast<jclass>(class_class_.value), for_name_,
                                       binary_name.value, JNI_FALSE, loader_.value)
        : nullptr;
    if (jni_->ExceptionCheck()) jni_->ExceptionClear();
    return loaded;
  }
  // A global reference owned by `batch`, or null when the loader cannot resolve it.
  jvmtiError Resolve(const std::string& descriptor, const std::string& target_descriptor,
                     Batch* batch, jclass* resolved_class, bool* same_runtime_package,
                     std::string* error) {
    *resolved_class = nullptr;
    LocalReference resolved(jni_), resolved_loader(jni_);
    resolved.value = Load(descriptor);
    if (!resolved.value) return JVMTI_ERROR_NONE;
    jvmtiError result = snapshot_env->GetClassLoader(static_cast<jclass>(resolved.value),
                                                     &resolved_loader.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    jobject root = jni_->NewGlobalRef(resolved.value);
    if (!root) {
      jni_->ExceptionClear();
      *error = "cannot retain a resolved type";
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    batch->type_roots.push_back(root);
    *resolved_class = static_cast<jclass>(root);
    *same_runtime_package = Package(descriptor) == Package(target_descriptor) &&
        jni_->IsSameObject(resolved_loader.value, loader_.value);
    return JVMTI_ERROR_NONE;
  }

 private:
  JNIEnv* jni_;
  jclass target_;
  LocalReference loader_;
  LocalReference class_class_;
  jmethodID for_name_ = nullptr;
};

// Facts about a new superclass that the policy needs: whether it extends the
// old superclass (widening) or the target, and the final methods that classes
// between it and the old superclass declare, as far as the target may override them.
jvmtiError DescribeSuperclass(JNIEnv* jni, jclass target, jclass superclass,
                              structural_policy::NewSuperclass* output, std::string* error) {
  LocalReference old_superclass(jni);
  old_superclass.value = jni->GetSuperclass(target);
  output->extends_target = jni->IsAssignableFrom(superclass, target);
  output->extends_old_superclass = old_superclass.value &&
      jni->IsAssignableFrom(superclass, static_cast<jclass>(old_superclass.value)) &&
      !jni->IsSameObject(superclass, old_superclass.value);
  if (!output->extends_old_superclass || output->extends_target) return JVMTI_ERROR_NONE;
  LocalReference current(jni);
  current.value = jni->NewLocalRef(superclass);
  while (current.value && !jni->IsSameObject(current.value, old_superclass.value)) {
    jclass klass = static_cast<jclass>(current.value);
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(snapshot_env, klass, &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    for (const auto& method : methods) {
      const bool overridable = (method.access & (evolution::kAccPublic | evolution::kAccProtected)) ||
          output->same_runtime_package;
      if (Virtual(method) && (method.access & evolution::kAccFinal) && overridable)
        output->final_methods.insert({method.name, method.descriptor});
    }
    jclass next = jni->GetSuperclass(klass);
    jni->DeleteLocalRef(current.value);
    current.value = next;
  }
  return JVMTI_ERROR_NONE;
}

evolution::DexView SubmittedDex(const jvmtiClassDefinition& definition) {
  const size_t size = definition.class_byte_count > 0 ? definition.class_byte_count : 0;
  return {definition.class_bytes, size, definition.class_bytes, size};
}

// ART recreates the target's loaded subclasses (FinishNewClassAllocations)
// before it verifies the new DEX, and its ClassDefinitionPauser lets this
// thread define classes meanwhile. A subclass that verification loads first
// would stay linked under the old class. Load, without initializing, every
// class the submitted DEX names, as verification may, through the target's
// defining loader. A type the loader cannot load is left to the verifier.
void PreloadReferencedTypes(const jvmtiClassDefinition& definition, TypeResolver* resolver,
                            const std::string& target_descriptor, JNIEnv* jni) {
  std::vector<std::string> classes;
  std::string unreadable;
  if (!evolution::ReadReferencedClasses(SubmittedDex(definition), &classes, &unreadable)) return;
  for (const auto& descriptor : classes) {
    if (descriptor == target_descriptor) continue;
    if (jobject loaded = resolver->Load(descriptor)) jni->DeleteLocalRef(loaded);
  }
}

// Reads the submitted definition and resolves the types it newly names.
// ClassLinker::LoadSuperAndInterfaces resolves a class's superclass and
// interfaces through its defining loader. AllocateNewClassObject skips that
// step, and LinkClass only looks interfaces up in the loader's class table, so
// each new type must be loaded through that loader first. A type left
// unresolved here is rejected by the policy in CheckClass, which reads the DEX
// after any ClassFileLoadHook.
jvmtiError ResolveNewTypes(const jvmtiClassDefinition& definition, TypeResolver* resolver,
                           Batch* batch, Snapshot* snapshot, std::string* error) {
  evolution::ClassSchema incoming;
  std::string unreadable;
  if (!evolution::ReadClassDeclarations(SubmittedDex(definition), &incoming, &unreadable))
    return JVMTI_ERROR_NONE;
  const auto& before = snapshot->live.interfaces;
  std::vector<std::string> added;
  for (const auto& descriptor : incoming.interfaces)
    if (std::find(before.begin(), before.end(), descriptor) == before.end())
      added.push_back(descriptor);
  const bool new_superclass = incoming.superclass != snapshot->live.superclass;
  if (added.empty() && !new_superclass) return JVMTI_ERROR_NONE;
  jvmtiError result = JVMTI_ERROR_NONE;
  for (const auto& descriptor : added) {
    jclass resolved = nullptr;
    structural_policy::ResolvedType type;
    result = resolver->Resolve(descriptor, snapshot->live.descriptor, batch, &resolved,
                              &type.same_runtime_package, error);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!resolved) continue;
    snapshot->hierarchy.added_interfaces[descriptor] = type;
    snapshot->interface_classes[descriptor] = resolved;
  }
  if (!new_superclass) return JVMTI_ERROR_NONE;
  structural_policy::NewSuperclass superclass;
  result = resolver->Resolve(incoming.superclass, snapshot->live.descriptor, batch,
                            &snapshot->superclass_class, &superclass.same_runtime_package, error);
  if (result != JVMTI_ERROR_NONE || !snapshot->superclass_class) return result;
  result = DescribeSuperclass(batch->jni, definition.klass, snapshot->superclass_class,
                              &superclass, error);
  if (result != JVMTI_ERROR_NONE) return result;
  snapshot->hierarchy.new_superclass = std::move(superclass);
  return JVMTI_ERROR_NONE;
}

jvmtiError InvokeStructural(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions,
                            transaction::Context* context) {
  const jvmtiError result = structural_entry(env, count, definitions);
  if (std::string reason = interface_redefinition::TakeRejection(); !reason.empty()) Diagnose(reason, true);
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
    std::lock_guard<std::mutex> lock(report_mutex);
    last_rejection.clear();
  }
  // ART retains responsibility for arguments, caller capabilities and the
  // ClassFileLoadHook. Snapshots precede its ScopedObjectAccess transition.
  if (count <= 0 || !definitions) return InvokeStructural(env, count, definitions, context);
  JNIEnv* jni = nullptr;
  if (runtime_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni)
    return JVMTI_ERROR_UNATTACHED_THREAD;
  batch.jni = jni;
  interface_redefinition::Begin(jni, snapshot_env, count, definitions);
  // ART rejects a missing or unmodifiable target before any validator runs;
  // do not load types for a batch it will not consider.
  for (jint i = 0; i < count; ++i) {
    jint access = 0;
    if (definitions[i].klass &&
        snapshot_env->GetClassModifiers(definitions[i].klass, &access) == JVMTI_ERROR_NONE &&
        (access & evolution::kAccInterface)) {
      if (interface_redefinition::Available()) continue;
      Diagnose(Signature(definitions[i].klass) + ": " + interface_redefinition::RuntimeState(), true);
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    jboolean modifiable = JNI_TRUE;
    if (!definitions[i].klass ||
        (snapshot_env->IsModifiableClass(definitions[i].klass, &modifiable) == JVMTI_ERROR_NONE &&
         !modifiable))
      return InvokeStructural(env, count, definitions, context);
  }
  // Loading types precedes the loaded-class view.
  jvmtiError result = JVMTI_ERROR_NONE;
  for (jint i = 0; i < count; ++i) {
    Snapshot snapshot;
    std::string error;
    const auto reject = [&](const std::string& reason) {
      Diagnose(Signature(definitions[i].klass) + ": " + reason, true);
    };
    const int read = live_schema::ReadLiveSchema(snapshot_env, jni, definitions[i].klass,
                                                &snapshot.live, &error);
    if (read != JVMTI_ERROR_NONE) { reject(error); return static_cast<jvmtiError>(read); }
    if (const RetiredKinds* earlier = FindRetiredKinds(jni, definitions[i].klass))
      snapshot.retired = earlier->methods;
    TypeResolver resolver(jni, definitions[i].klass);
    result = resolver.Init(&error);
    if (result == JVMTI_ERROR_NONE) {
      PreloadReferencedTypes(definitions[i], &resolver, snapshot.live.descriptor, jni);
      result = ResolveNewTypes(definitions[i], &resolver, &batch, &snapshot, &error);
    }
    if (result != JVMTI_ERROR_NONE) { reject(error); return result; }
    interface_redefinition::Describe(definitions[i].klass, snapshot.live);
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
  LocalClasses loaded(snapshot_env, jni);
  const auto loaded_start = std::chrono::steady_clock::now();
  result = snapshot_env->GetLoadedClasses(&loaded.count, &loaded.classes);
  // Failures without a reason here are described by Route.
  if (result != JVMTI_ERROR_NONE) return result;
  std::string interface_error;
  const uint64_t loaded_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - loaded_start).count();
  result = interface_redefinition::Capture(loaded.count, loaded.classes, loaded_ns, &interface_error);
  if (result != JVMTI_ERROR_NONE) {
    Diagnose(interface_error, true);
    return result;
  }
  for (auto& [klass, snapshot] : batch.classes) {
    std::string error;
    const jclass* candidates = loaded.classes;
    jint candidate_count = loaded.count;
    if (snapshot.live.access & evolution::kAccInterface) {
      const std::vector<jclass>* group = interface_redefinition::GroupTypes(klass);
      if (!group) {
        Diagnose(snapshot.live.descriptor + ": interface subtype group was not captured", true);
        return JVMTI_ERROR_INTERNAL;
      }
      candidates = group->data();
      candidate_count = static_cast<jint>(group->size());
    }
    result = CollectSubclasses(jni, klass, candidates, candidate_count, &snapshot, &error);
    if (result != JVMTI_ERROR_NONE) {
      Diagnose(snapshot.live.descriptor + ": " + error, true);
      return result;
    }
  }
  if (member_mapping::Available()) {
    // Build the permanent roots while Native, before ART's no-failure point.
    // Root all affected loaded subclasses: each replacement ClassExt retains
    // its own obsolete chain, including native ArtMethod storage and DexCache.
    for (jint i = 0; i < loaded.count; ++i) {
      jclass candidate = loaded.classes[i];
      bool affected = false;
      for (jint j = 0; j < count && !affected; ++j)
        affected = jni->IsAssignableFrom(candidate, definitions[j].klass);
      if (!affected) continue;
      if (member_retirement::Rooted(jni, candidate)) continue;
      jobject root = jni->NewGlobalRef(candidate);
      if (!root) {
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
      batch.retirement_roots.push_back(root);
    }
  }
  result = InvokeStructural(env, count, definitions, context);
  *subclass_loaded = watch.SubclassLoaded();
  return result;
}

// Preloading covers the classes the submitted DEX names. A ClassFileLoadHook
// may name others, and another thread may load a subclass before ART pauses
// class definition. Such an attempt fails before ART's no-failure point and
// leaves the class loaded, so the next attempt includes it.
jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) noexcept try {
  std::lock_guard<std::recursive_mutex> transaction_lock(transaction_mutex);
  constexpr int kAttempts = 3;
  for (int attempt = 1;; ++attempt) {
    bool subclass_loaded = false;
    const jvmtiError result = RouteOnce(env, count, definitions, &subclass_loaded);
    if (result == JVMTI_ERROR_NONE) return result;
    if (subclass_loaded && attempt < kAttempts) {
      subclass_retries.fetch_add(1);
      continue;
    }
    // Every failure names its classes, whichever check rejected it.
    bool explained;
    {
      std::lock_guard<std::mutex> lock(report_mutex);
      explained = !last_rejection.empty();
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
  if (!batch) return reinterpret_cast<Check>(original_class)(self);
  batch->decisions.erase(self);
  const auto found = batch->classes.find(self->klass);
  if (found == batch->classes.end() || !self->dex_file || !self->dex_file->begin)
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
  abilities.member_mapping = class_layout::VerifyTarget(self, snapshot.live);
  for (auto& [descriptor, type] : snapshot.hierarchy.added_interfaces) {
    uint32_t access = 0;
    type.access.reset();
    if (class_layout::ClassAccess(self, snapshot.interface_classes.at(descriptor), &access))
      type.access = access;
  }
  if (auto& superclass = snapshot.hierarchy.new_superclass) {
    uint32_t access = 0;
    superclass->access.reset();
    if (class_layout::ClassAccess(self, snapshot.superclass_class, &access))
      superclass->access = access;
  }
  abilities.field_retirement =
      abilities.member_mapping && member_retirement::FieldRetirementAvailable();
  abilities.instance_check = member_mapping::Available();
  abilities.superclass_change =
      abilities.member_mapping && member_mapping::SuperclassChangeAvailable();
  // The snapshot carries the interface group's facts, the mapping rejects new
  // default conflicts, and retirement roots cover the group: an interface
  // target may evolve its members whenever the engine owns the mapping.
  abilities.interface_members = abilities.member_mapping;
  abilities.interface_abstract_methods =
      (snapshot.live.access & evolution::kAccInterface) && abilities.member_mapping &&
      interface_dispatch::Available(self);
  if (!abilities.interface_abstract_methods)
    abilities.interface_abstract_unavailable_reason = interface_dispatch::State();
  auto decision = structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy,
                                              abilities, snapshot.retired);
  const bool interface_target = interface_redefinition::IsTarget(self);
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
      interface_redefinition::SharesImplementationGroup(self)) {
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
  structural_policy::Abilities without_mapping;
  without_mapping.instance_check = abilities.instance_check;
  transaction::Requirements& requirements = context->targets[self];
  requirements.descriptor = incoming.descriptor;
  requirements.member_mapping = interface_target ||
      (abilities.member_mapping &&
       structural_policy::Evaluate(snapshot.live, incoming, snapshot.hierarchy, without_mapping,
                                   snapshot.retired).error != JVMTI_ERROR_NONE);
  requirements.no_instances = decision.requires_no_instances;
  for (const auto& [name, descriptor] : decision.interface_obligations)
    context->interface_obligations.push_back({self, name, descriptor});
  // The policy rejects a superclass change related to another batch target,
  // so an interface group member never also changes its superclass.
  requirements.superclass = decision.superclass_changed ? snapshot.superclass_class : nullptr;
  requirements.redirect_verification =
      decision.superclass_changed || interface_redefinition::IsAffected(self);
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
      if (found->second.fields_structural) record_field(self);
      return true;
    }
  }
  return reinterpret_cast<Check>(original_fields)(self);
}

bool CheckMethods(ClassRedefinitionPrefix* self) noexcept {
  if (Batch* batch = BatchOf(transaction::Current())) {
    const auto found = batch->decisions.find(self);
    if (found != batch->decisions.end()) {
      if (found->second.methods_structural) record_method(self);
      batch->decisions.erase(found);
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
                   const member_retirement::FieldPlaceholders& placeholders) {
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
  std::vector<Hook> replacements;
  std::string retirement_note;
  member_mapping::Prepare(vm, env, &replacements, &retirement_note);
  std::string interface_note;
  interface_redefinition::Prepare(member_mapping::SuperclassChangeAvailable(), &replacements,
                                  &interface_note);
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
  // The engine's own environment carries no other events.
  jvmtiEventCallbacks callbacks{};
  callbacks.ClassPrepare = ClassPrepared;
  watch_available = member_mapping::Available() &&
      env->SetEventCallbacks(&callbacks, sizeof(callbacks)) == JVMTI_ERROR_NONE;
  std::string field_note;
  if (member_mapping::Available())
    member_retirement::ResolveFieldPlaceholders(env, placeholders, &field_note);
  std::lock_guard<std::mutex> lock(report_mutex);
  installed = true;
  diagnostic = !member_mapping::Available()
      ? "ART structural transaction with additions and modifier changes; members cannot retire"
      : member_retirement::FieldRetirementAvailable()
          ? "ART structural transaction with method and field retirement"
          : "ART structural transaction with method retirement; fields cannot retire";
  for (const auto* note : {&retirement_note, &field_note, &interface_note})
    if (!note->empty()) diagnostic += "; " + *note;
  return JVMTI_ERROR_NONE;
}

std::string Capabilities() {
  std::lock_guard<std::mutex> lock(report_mutex);
  const std::string supported = installed ? "true" : "false";
  const bool methods = installed && member_mapping::Available();
  const bool fields = methods && member_retirement::FieldRetirementAvailable();
  const std::string retirement = methods ? "true" : "false";
  const std::string field_retirement = fields ? "true" : "false";
  const auto interface_timing = interface_redefinition::LastTiming();
  // Both capability fields describe the same probe snapshot, including disablement.
  const std::string interface_state = interface_redefinition::RuntimeState();
  const std::string dispatch_state = interface_dispatch::State();
  return "{\"installed\":" + supported + ",\"standardRedefineRouted\":" + supported +
      ",\"memberAddition\":" + supported + ",\"memberModifierChange\":" + supported +
      ",\"classModifierChange\":" + supported + ",\"interfaceAddition\":" + retirement + ",\"superclassWidening\":" +
          (methods && member_mapping::SuperclassChangeAvailable() ? "true" : "false") +
      ",\"methodDeletion\":" + retirement + ",\"methodSignatureChange\":" + retirement +
      ",\"fieldDeletion\":" + field_retirement + ",\"fieldTypeChange\":" + field_retirement +
      ",\"memberMapping\":" + QuoteJsonMutf8(class_layout::State()) +
      ",\"jniRetirementGuards\":" +
          std::to_string(member_retirement::JniRetirementGuardHits()) +
      ",\"subclassLoadRetries\":" + std::to_string(subclass_retries.load()) +
      ",\"interfaceRedefinition\":" + (installed && interface_state.rfind("verified: ", 0) == 0 ? "true" : "false") +
      ",\"interfaceRuntime\":" + QuoteJsonMutf8(interface_state) +
      ",\"interfaceAbstractMethodAddition\":" +
          (installed && interface_state.rfind("verified: ", 0) == 0 &&
           dispatch_state.rfind("verified: ", 0) == 0 && interface_redefinition::AbstractLinkErrorsAvailable()
               ? "true" : "false") +
      ",\"interfaceAbstractDispatch\":" + QuoteJsonMutf8(dispatch_state) +
      ",\"interfaceSnapshotTypes\":" + std::to_string(interface_timing.types) +
      ",\"interfaceSnapshotNanos\":" + std::to_string(interface_timing.snapshot_ns) +
      ",\"interfaceAllocationNanos\":" + std::to_string(interface_timing.allocation_ns) +
      ",\"memberDeletion\":" + field_retirement + ",\"memberTypeChange\":" + field_retirement +
      ",\"hierarchyChange\":false,"
      "\"fullDcevmComplete\":false,\"diagnostic\":" + QuoteJsonMutf8(diagnostic) +
      ",\"lastRejection\":" + QuoteJsonMutf8(last_rejection) + "}";
}
}  // namespace artdcevm::structural_redefine
