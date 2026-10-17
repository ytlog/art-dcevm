#include "platform/common/interface_redefinition_internal.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
void RuntimeBackend::Begin(JNIEnv* jni, jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) {
  transaction::Context* context = transaction::Current();
  if (!context) return;
  context->interfaces = std::make_unique<State>(jni, env, count, definitions);
  auto* next = static_cast<State*>(context->interfaces.get());
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(16)) throw std::bad_alloc{};
)
  if (!RuntimeBackend::Available() || count <= 0 || !definitions) return;
  for (jint i = 0; i < count; ++i) {
    jint access = 0, status = 0;
    jobject loader = nullptr;
    jclass klass = definitions[i].klass;
    if (!klass || env->GetClassModifiers(klass, &access) != JVMTI_ERROR_NONE || !(access & evolution::kAccInterface)) continue;
    if (env->GetClassLoader(klass, &loader) != JVMTI_ERROR_NONE) continue;
    const auto status_result = env->GetClassStatus(klass, &status);
    const bool boot = !loader;
    if (loader) jni->DeleteLocalRef(loader);
    if (status_result != JVMTI_ERROR_NONE) continue;
    next->targets.push_back({klass, boot, status, {}, {}, nullptr, nullptr});
  }
  if (!next->targets.empty() && jni->EnsureLocalCapacity(8) == JNI_OK) {
    if (jni->GetJavaVM(&next->vm) == JNI_OK)
      next->class_class = jni->FindClass("java/lang/Class");
    // Resolve Class[] before the subclass-load watch starts.
    jobjectArray array = next->class_class ? jni->NewObjectArray(0, next->class_class, nullptr) : nullptr;
    if (array) jni->DeleteLocalRef(array);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
}
const std::vector<jclass>* RuntimeBackend::GroupTypes(jclass klass) {
  const State* current = CurrentState();
  if (!current) return nullptr;
  for (const auto& target : current->targets) {
    if (!target.owner || !current->jni->IsSameObject(target.klass, klass)) continue;
    for (const auto& owner : current->targets)
      if (current->jni->IsSameObject(owner.klass, target.owner)) return &owner.snapshot.types;
  }
  return nullptr;
}
void RuntimeBackend::Describe(jclass klass, const evolution::ClassSchema& live) {
  State* next = CurrentState();
  if (!next) return;
  for (auto& target : next->targets)
    if (next->jni->IsSameObject(target.klass, klass)) target.live = live;
}
// A fresh app_process can use plain pointer IDs without ClassExt markers.
// Probe one existing member ID while Native; an indexed ID establishes the
// runtime mode. Per-class legacy markers are still checked by ART admission.
template <class Id>
jvmtiError VerifyJniIdMode(jvmtiEnv* env, const TypeSnapshot& snapshot,
                         jvmtiError (jvmtiEnv::*get_ids)(jclass, jint*, Id**),
                         bool* verified, std::string* error) {
  for (size_t i = 0; !*verified && i < snapshot.types.size(); ++i) {
    jint count = 0;
    Id* ids = nullptr;
    const auto result = (env->*get_ids)(snapshot.types[i], &count, &ids);
    const auto release = [env](Id* ids) {
      if (ids) env->Deallocate(reinterpret_cast<unsigned char*>(ids));
    };
    std::unique_ptr<Id[], decltype(release)> allocation(ids, release);
    if (result != JVMTI_ERROR_NONE) {
      *error = snapshot.descriptors[i] + ": interface JNI ID representation probe failed";
      return result;
    }
    if (count == 0) continue;
    if (!ids || !ids[0]) {
      *error = snapshot.descriptors[i] + ": interface JNI member ID is unavailable";
      return JVMTI_ERROR_INTERNAL;
    }
    if (!IsIndexedJniId(ids[0])) {
      *error = snapshot.descriptors[i] + ": active pointer JNI IDs prevent structural redefinition";
      return JVMTI_ERROR_UNMODIFIABLE_CLASS;
    }
    *verified = true;
  }
  return JVMTI_ERROR_NONE;
}

jvmtiError RuntimeBackend::Capture(jint count, const jclass* loaded, uint64_t loaded_ns, std::string* error) {
  const auto start = std::chrono::steady_clock::now();
  timing = {};
ARTDCEVM_TEST(
  fault_injection::Set(fault_injection::Counter::kAllocationOrder, 0);
  fault_injection::Set(fault_injection::Counter::kAllocatedTypes, 0);
  fault_injection::Set(fault_injection::Counter::kAllocatedGroups, 0);
)
  State* next = CurrentState();
  if (!next) return JVMTI_ERROR_NONE;
  // A batch child interface belongs to its highest batch ancestor. Only roots
  // capture a group, so each child interface/implementation is rebuilt once.
  for (auto& target : next->targets) {
    target.owner = target.klass;
    for (const auto& other : next->targets)
      if (next->jni->IsAssignableFrom(target.owner, other.klass)) target.owner = other.klass;
    if (next->jni->ExceptionCheck()) {
      next->jni->ExceptionClear();
      *error = target.live.descriptor + ": interface batch ownership check failed";
      return JVMTI_ERROR_INTERNAL;
    }
  }
  bool indexed_jni_ids = false;
  for (auto& target : next->targets) {
    if (!next->jni->IsSameObject(target.owner, target.klass)) continue;
    jvmtiError result = SnapshotTypes(next->jni, next->env, target.klass,
                                      count, loaded, &target.snapshot, error);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!target.boot) {
      result = VerifyJniIdMode(next->env, target.snapshot, &jvmtiEnv::GetClassMethods,
                              &indexed_jni_ids, error);
      if (result == JVMTI_ERROR_NONE && !indexed_jni_ids)
        result = VerifyJniIdMode(next->env, target.snapshot, &jvmtiEnv::GetClassFields,
                                &indexed_jni_ids, error);
      if (result != JVMTI_ERROR_NONE) return result;
    }
    timing.types += target.snapshot.types.size();
  }
  // SnapshotTypes borrows the exact GetLoadedClasses references. Identity
  // keys here detect overlap in one pass without pairwise type comparisons.
  std::unordered_map<jclass, const Target*> owners;
  for (const auto& target : next->targets) {
    for (jclass type : target.snapshot.types) {
      if (!owners.emplace(type, &target).second) {
        *error = target.live.descriptor + ": unrelated interface groups share a subtype";
        return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
      }
    }
  }
  for (const auto& target : next->targets) {
    for (jint i = 0; i < next->batch_size; ++i) {
      jclass other = next->definitions[i].klass;
      if (next->jni->IsAssignableFrom(other, target.klass)) continue;
      // A class outside the interface's type relation can still share its
      // implementation subtree. Two structural owners would rebuild it twice.
      for (jclass type : target.snapshot.types) {
        if (!next->jni->IsAssignableFrom(type, other)) continue;
        next->shared_ancestors.push_back(other);
        break;
      }
      if (next->jni->ExceptionCheck()) {
        next->jni->ExceptionClear();
        *error = target.live.descriptor + ": interface batch group-overlap check failed";
        return JVMTI_ERROR_INTERNAL;
      }
    }
  }
  if (!next->targets.empty() && next->jni->EnsureLocalCapacity(8) != JNI_OK) {
    if (next->jni->ExceptionCheck()) next->jni->ExceptionClear();
    *error = "interface snapshot local reference reservation failed";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  for (auto& target : next->targets) {
    if (!next->jni->IsSameObject(target.owner, target.klass)) continue;
    if (!next->vm || !next->class_class) {
      *error = target.live.descriptor + ": interface private array JNI services unavailable";
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    if (target.snapshot.types.size() > static_cast<size_t>(std::numeric_limits<jsize>::max())) {
      *error = target.live.descriptor + ": interface private arrays are too large";
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    target.arrays = ClassArrayRoots::Create(next->jni, next->vm, next->class_class,
                                           static_cast<jsize>(target.snapshot.types.size()));
    if (!target.arrays) {
      *error = target.live.descriptor + ": interface private array allocation failed while Native";
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
  }
  if (!next->targets.empty())
    timing.snapshot_ns = loaded_ns + std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
  return JVMTI_ERROR_NONE;
}

}  // namespace artdcevm::interface_redefinition
