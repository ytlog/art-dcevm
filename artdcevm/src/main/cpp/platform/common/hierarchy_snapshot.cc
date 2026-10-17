#include "platform/common/hierarchy_snapshot.h"
#include "platform/common/schema_snapshot.h"
#include <algorithm>
#include <string_view>

namespace artdcevm::hierarchy_snapshot {
namespace {
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

std::string_view Package(const std::string& descriptor) {
  const size_t end = descriptor.rfind('/');
  return end == std::string::npos ? std::string_view{} : std::string_view(descriptor.data(), end);
}

bool Virtual(const evolution::Member& method) {
  return !method.IsStatic() && !(method.access & evolution::kAccPrivate) &&
      method.name != "<init>" && method.name != "<clinit>";
}

// Instance methods of `klass`'s interfaces and their superinterfaces.
jvmtiError CollectInterfaceMethods(jvmtiEnv* env, JNIEnv* jni, jclass klass, std::set<std::string>* visited,
                                   std::set<structural_policy::MemberKey>* output,
                                   std::string* error) {
  LocalClasses interfaces(env, jni);
  jvmtiError result = env->GetImplementedInterfaces(klass, &interfaces.count,
                                                             &interfaces.classes);
  if (result != JVMTI_ERROR_NONE) { *error = "GetImplementedInterfaces failed"; return result; }
  for (jint i = 0; i < interfaces.count; ++i) {
    char* signature = nullptr;
    result = env->GetClassSignature(interfaces.classes[i], &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const bool first = visited->insert(signature).second;
    env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    if (!first) continue;
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(env, interfaces.classes[i], &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    for (const auto& method : methods)
      if (Virtual(method)) output->insert({method.name, method.descriptor});
    result = CollectInterfaceMethods(env, jni, interfaces.classes[i], visited, output, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  return JVMTI_ERROR_NONE;
}

jvmtiError CollectInheritedPublic(jvmtiEnv* env, JNIEnv* jni, jclass target, Snapshot* snapshot,
                                  std::string* error) {
  LocalReference current(jni);
  current.value = jni->GetSuperclass(target);
  while (current.value) {
    jclass klass = static_cast<jclass>(current.value);
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(env, klass, &methods, error);
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
jvmtiError CollectInheritedInterface(jvmtiEnv* env, JNIEnv* jni, jclass target, Snapshot* snapshot,
                                     std::string* error) {
  auto& inherited = snapshot->hierarchy.inherited_interface;
  std::set<std::string> visited;
  jvmtiError result = CollectInterfaceMethods(env, jni, target, &visited, &inherited, error);
  if (result != JVMTI_ERROR_NONE) return result;
  LocalReference object(jni);
  object.value = jni->FindClass("java/lang/Object");
  if (!object.value) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = "java.lang.Object unavailable";
    return JVMTI_ERROR_INTERNAL;
  }
  std::vector<evolution::Member> methods;
  const int read = live_schema::ReadLiveMethods(env, static_cast<jclass>(object.value),
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
jvmtiError CollectSuperclassShadows(jvmtiEnv* env, JNIEnv* jni, jclass target, jclass implementation,
                                    std::set<std::string>* boot_visited, Snapshot* snapshot,
                                    std::string* error) {
  LocalReference current(jni);
  current.value = jni->GetSuperclass(implementation);
  while (current.value) {
    jclass klass = static_cast<jclass>(current.value);
    if (jni->IsAssignableFrom(klass, target)) break;
    LocalReference loader(jni);
    jvmtiError result = env->GetClassLoader(klass, &loader.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    char* signature = nullptr;
    result = env->GetClassSignature(klass, &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const std::string descriptor = signature;
    env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    if (loader.value || boot_visited->insert(descriptor).second) {
      std::vector<evolution::Member> methods;
      const int read = live_schema::ReadLiveMethods(env, klass, &methods, error);
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
jvmtiError CollectFacts(jvmtiEnv* env, JNIEnv* jni, jclass target, const std::vector<LoadedType>& candidates,
                             Snapshot* snapshot, std::string* error) {
  const bool interface_target = snapshot->live.access & evolution::kAccInterface;
  std::set<std::string> boot_visited;
  LocalReference target_loader(jni);
  jvmtiError result = env->GetClassLoader(target, &target_loader.value);
  if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
  for (const auto& type : candidates) {
    jclass candidate = type.klass;
    if (jni->IsSameObject(candidate, target) || !type.prepared) continue;
    snapshot->hierarchy.loaded_subclasses = true;
    std::vector<evolution::Member> methods;
    const int read = live_schema::ReadLiveMethods(env, candidate, &methods, error);
    if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
    char* signature = nullptr;
    result = env->GetClassSignature(candidate, &signature, nullptr);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassSignature failed"; return result; }
    const std::string descriptor = signature;
    env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    const bool same_package = Package(snapshot->live.descriptor) == Package(descriptor);
    bool same_runtime_package = false;
    if (same_package) {
      LocalReference candidate_loader(jni);
      result = env->GetClassLoader(candidate, &candidate_loader.value);
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
    result = CollectInterfaceMethods(env, jni, candidate, &visited, &interface_methods, error);
    if (result != JVMTI_ERROR_NONE) return result;
    for (const auto& key : interface_methods)
      if (!declared.count(key)) snapshot->hierarchy.interface_copies.insert(key);
    if (!interface_target) continue;
    AddShadows(methods, descriptor, snapshot);
    jboolean is_interface = JNI_FALSE;
    result = env->IsInterface(candidate, &is_interface);
    if (result != JVMTI_ERROR_NONE) { *error = "IsInterface failed"; return result; }
    if (is_interface) continue;
    result = CollectSuperclassShadows(env, jni, target, candidate, &boot_visited, snapshot, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  if (interface_target) return CollectInheritedInterface(env, jni, target, snapshot, error);
  return CollectInheritedPublic(env, jni, target, snapshot, error);
}

// Class.forName(name, false, loader) through a target's defining loader. It
// runs ClassLinker::FindClass, which records the loader as an initiating loader
// of the class it returns, and loads without initializing.
class TypeResolver {
 public:
  TypeResolver(jvmtiEnv* env, JNIEnv* jni, jclass target) : env_(env), jni_(jni), target_(target), loader_(jni),
                                             class_class_(jni) {}
  jvmtiError Init(std::string* error) {
    jvmtiError result = env_->GetClassLoader(target_, &loader_.value);
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
  // A global reference owned by `roots`, or null when the loader cannot resolve it.
  jvmtiError Resolve(const std::string& descriptor, const std::string& target_descriptor,
                     std::vector<jobject>* roots, jclass* resolved_class, bool* same_runtime_package,
                     std::string* error) {
    *resolved_class = nullptr;
    LocalReference resolved(jni_), resolved_loader(jni_);
    resolved.value = Load(descriptor);
    if (!resolved.value) return JVMTI_ERROR_NONE;
    jvmtiError result = env_->GetClassLoader(static_cast<jclass>(resolved.value),
                                                     &resolved_loader.value);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return result; }
    jobject root = jni_->NewGlobalRef(resolved.value);
    if (!root) {
      jni_->ExceptionClear();
      *error = "cannot retain a resolved type";
      return JVMTI_ERROR_OUT_OF_MEMORY;
    }
    roots->push_back(root);
    *resolved_class = static_cast<jclass>(root);
    *same_runtime_package = Package(descriptor) == Package(target_descriptor) &&
        jni_->IsSameObject(resolved_loader.value, loader_.value);
    return JVMTI_ERROR_NONE;
  }

 private:
  jvmtiEnv* env_;
  JNIEnv* jni_;
  jclass target_;
  LocalReference loader_;
  LocalReference class_class_;
  jmethodID for_name_ = nullptr;
};

// Facts about a new superclass that the policy needs: whether it extends the
// old superclass (widening) or the target, and the final methods that classes
// between it and the old superclass declare, as far as the target may override them.
jvmtiError DescribeSuperclass(jvmtiEnv* env, JNIEnv* jni, jclass target, jclass superclass,
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
    const int read = live_schema::ReadLiveMethods(env, klass, &methods, error);
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
                           jvmtiEnv* env, JNIEnv* jni, std::vector<jobject>* roots, Snapshot* snapshot, std::string* error) {
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
    result = resolver->Resolve(descriptor, snapshot->live.descriptor, roots, &resolved,
                              &type.same_runtime_package, error);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!resolved) continue;
    snapshot->hierarchy.added_interfaces[descriptor] = type;
    snapshot->interface_classes[descriptor] = resolved;
  }
  if (!new_superclass) return JVMTI_ERROR_NONE;
  structural_policy::NewSuperclass superclass;
  result = resolver->Resolve(incoming.superclass, snapshot->live.descriptor, roots,
                            &snapshot->superclass_class, &superclass.same_runtime_package, error);
  if (result != JVMTI_ERROR_NONE || !snapshot->superclass_class) return result;
  result = DescribeSuperclass(env, jni, definition.klass, snapshot->superclass_class,
                              &superclass, error);
  if (result != JVMTI_ERROR_NONE) return result;
  snapshot->hierarchy.new_superclass = std::move(superclass);
  return JVMTI_ERROR_NONE;
}

}  // namespace

jvmtiError BuildIndex(jvmtiEnv* env, JNIEnv* jni, jint count, const jclass* loaded,
                      const std::vector<jclass>& ordinary_targets,
                      const std::vector<jclass>& interface_targets,
                      LoadedIndex* output, std::string* error) {
  const auto fail = [&](jvmtiError code, const char* reason) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = reason;
    return code;
  };
  LoadedIndex next;
  next.subtypes.resize(ordinary_targets.size());
  for (jint i = 0; i < count; ++i) {
    bool affected = false;
    bool prepared = false;
    for (size_t target = 0; target < ordinary_targets.size(); ++target) {
      if (!jni->IsAssignableFrom(loaded[i], ordinary_targets[target])) continue;
      // Read status once for all matching ordinary targets. Retention includes
      // every status; only policy facts use the PREPARED bit.
      if (!affected) {
        jint status = 0;
        const auto result = env->GetClassStatus(loaded[i], &status);
        if (result != JVMTI_ERROR_NONE) return fail(result, "GetClassStatus failed");
        prepared = (status & JVMTI_CLASS_STATUS_PREPARED) != 0;
      }
      next.subtypes[target].push_back({loaded[i], prepared});
      affected = true;
    }
    for (jclass target : interface_targets) {
      if (affected) break;
      affected = jni->IsAssignableFrom(loaded[i], target);
    }
    if (affected) next.affected.push_back(loaded[i]);
  }
  *output = std::move(next);
  return JVMTI_ERROR_NONE;
}

jvmtiError Prepare(jvmtiEnv* env, JNIEnv* jni, const jvmtiClassDefinition& definition,
                   Snapshot* snapshot, std::vector<jobject>* roots, std::string* error) {
  TypeResolver resolver(env, jni, definition.klass);
  jvmtiError result = resolver.Init(error);
  if (result != JVMTI_ERROR_NONE) return result;
  PreloadReferencedTypes(definition, &resolver, snapshot->live.descriptor, jni);
  return ResolveNewTypes(definition, &resolver, env, jni, roots, snapshot, error);
}

jvmtiError CollectSubclasses(jvmtiEnv* env, JNIEnv* jni, jclass target,
                             const std::vector<LoadedType>& candidates,
                             Snapshot* snapshot, std::string* error) {
  return CollectFacts(env, jni, target, candidates, snapshot, error);
}

jvmtiError CollectInterfaceSubclasses(jvmtiEnv* env, JNIEnv* jni, jclass target,
                                      const std::vector<jclass>& group,
                                      Snapshot* snapshot, std::string* error) {
  std::vector<LoadedType> candidates;
  for (jclass klass : group) {
    if (jni->IsSameObject(klass, target) || !jni->IsAssignableFrom(klass, target)) continue;
    jint status = 0;
    const auto result = env->GetClassStatus(klass, &status);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassStatus failed"; return result; }
    candidates.push_back({klass, (status & JVMTI_CLASS_STATUS_PREPARED) != 0});
  }
  return CollectFacts(env, jni, target, candidates, snapshot, error);
}
}  // namespace artdcevm::hierarchy_snapshot
