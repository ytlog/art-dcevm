#include "platform/common/hierarchy_snapshot.h"
#include "evolution/modifiers.h"
#include "platform/common/schema_snapshot.h"
#include "platform/common/bound_method_handle.h"
#include "platform/common/jni_interface_dispatch.h"
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
// java.lang.Object (inherited_interface and inherited_public respectively).
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
      snapshot->hierarchy.inherited_public.insert({method.name, method.descriptor});
  return JVMTI_ERROR_NONE;
}

// Class-chain declarations incompatible with a new default
// (LoadedHierarchy::subtype_shadows). A new default must preserve both
// class-chain resolution and interface selection. ART 15--17 ignores
// non-public virtual implementations and copies the default where JBR would
// throw IllegalAccessError; older ART rejects these while linking. A package
// member in another runtime package also changes class-chain resolution.
// Track the most specific virtual declaration so a public override remains
// admissible even when an ancestor has a non-public method.
using ShadowKeys = std::set<structural_policy::MemberKey>;
using BootMethods = std::map<std::string, std::vector<evolution::Member>>;
void AddShadows(const std::vector<evolution::Member>& methods, const std::string& descriptor,
                bool other_package, ShadowKeys* resolved, Snapshot* snapshot) {
  constexpr uint32_t kAccess = evolution::kAccPublic | evolution::kAccProtected | evolution::kAccPrivate;
  for (const auto& method : methods) {
    if (method.name == "<init>" || method.name == "<clinit>") continue;
    const structural_policy::MemberKey key{method.name, method.descriptor};
    if (resolved->count(key)) continue;
    const bool virtual_method = Virtual(method);
    if ((virtual_method && !(method.access & evolution::kAccPublic)) ||
        (other_package && !(method.access & kAccess)))
      snapshot->hierarchy.subtype_shadows.emplace(key, descriptor);
    if (virtual_method) resolved->insert(key);
  }
}

// Non-boot descriptors may repeat across loaders. Only boot declarations are
// cached; their effective selection is still evaluated for each implementation.
jvmtiError CollectSuperclassShadows(jvmtiEnv* env, JNIEnv* jni, jclass target, jclass implementation,
                                    const std::string& implementation_descriptor, ShadowKeys* resolved,
                                    BootMethods* boot_methods, Snapshot* snapshot, std::string* error) {
  LocalReference implementation_loader(jni);
  jvmtiError loaded = env->GetClassLoader(implementation, &implementation_loader.value);
  if (loaded != JVMTI_ERROR_NONE) { *error = "GetClassLoader failed"; return loaded; }
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
    const bool other_package = Package(descriptor) != Package(implementation_descriptor) ||
        !jni->IsSameObject(loader.value, implementation_loader.value);
    std::vector<evolution::Member> methods;
    const auto cached = boot_methods->find(descriptor);
    if (!loader.value && cached != boot_methods->end()) methods = cached->second;
    else {
      const int read = live_schema::ReadLiveMethods(env, klass, &methods, error);
      if (read != JVMTI_ERROR_NONE) return static_cast<jvmtiError>(read);
      if (!loader.value) boot_methods->emplace(descriptor, methods);
    }
    AddShadows(methods, descriptor, other_package, resolved, snapshot);
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
  BootMethods boot_methods;
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
    // Its own package-private methods may be skipped for its subclasses in
    // other packages, which the walk below does not compare against it.
    ShadowKeys resolved;
    AddShadows(methods, descriptor, true, &resolved, snapshot);
    jboolean is_interface = JNI_FALSE;
    result = env->IsInterface(candidate, &is_interface);
    if (result != JVMTI_ERROR_NONE) { *error = "IsInterface failed"; return result; }
    if (is_interface) continue;
    result = CollectSuperclassShadows(env, jni, target, candidate, descriptor, &resolved, &boot_methods, snapshot, error);
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
                              bool interface_target, structural_policy::NewSuperclass* output, std::string* error) {
  LocalReference old_superclass(jni);
  old_superclass.value = jni->GetSuperclass(target);
  // JNI exposes no superclass for an interface. DEX and ART's Class linker
  // both use Object, including when the replacement is an ordinary class.
  if (!old_superclass.value && interface_target) {
    old_superclass.value = jni->FindClass("java/lang/Object");
    if (!old_superclass.value) {
      jni->ExceptionClear();
      *error = "interface Object superclass unavailable";
      return JVMTI_ERROR_INTERNAL;
    }
  }
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

// Interface targets: the virtual methods each added parent brings. A parent
// the target already inherits contributes nothing new, nor do its own parents.
jvmtiError DescribeParentMethods(jvmtiEnv* env, JNIEnv* jni, jclass target, std::vector<jclass> pending,
                                 std::vector<jobject>* roots, Snapshot* snapshot, std::string* error) {
  std::vector<jclass> visited;
  while (!pending.empty()) {
    jclass parent = pending.back();
    pending.pop_back();
    bool seen = jni->IsAssignableFrom(target, parent);
    for (jclass other : visited) seen = seen || jni->IsSameObject(other, parent);
    if (seen) continue;
    visited.push_back(parent);
    char* signature = nullptr;
    jvmtiError result = env->GetClassSignature(parent, &signature, nullptr);
    if (result != JVMTI_ERROR_NONE || !signature) { *error = "GetClassSignature failed"; return result ? result : JVMTI_ERROR_INTERNAL; }
    const std::string descriptor(signature);
    env->Deallocate(reinterpret_cast<unsigned char*>(signature));
    jint count = 0;
    jmethodID* methods = nullptr;
    result = env->GetClassMethods(parent, &count, &methods);
    if (result != JVMTI_ERROR_NONE) { *error = "GetClassMethods failed"; return result; }
    auto& described = snapshot->hierarchy.parent_methods[descriptor];
    for (jint i = 0; i < count && result == JVMTI_ERROR_NONE; ++i) {
      jint modifiers = 0;
      char* name = nullptr;
      char* method_signature = nullptr;
      result = env->GetMethodModifiers(methods[i], &modifiers);
      if (result == JVMTI_ERROR_NONE) result = env->GetMethodName(methods[i], &name, &method_signature, nullptr);
      if (result == JVMTI_ERROR_NONE && !(modifiers & (evolution::kAccStatic | evolution::kAccPrivate)))
        described.push_back({{name, method_signature}, (modifiers & evolution::kAccAbstract) != 0});
      env->Deallocate(reinterpret_cast<unsigned char*>(name));
      env->Deallocate(reinterpret_cast<unsigned char*>(method_signature));
    }
    env->Deallocate(reinterpret_cast<unsigned char*>(methods));
    if (result != JVMTI_ERROR_NONE) { *error = "parent interface method description failed"; return result; }
    jint interface_count = 0;
    jclass* interfaces = nullptr;
    result = env->GetImplementedInterfaces(parent, &interface_count, &interfaces);
    if (result != JVMTI_ERROR_NONE) { *error = "GetImplementedInterfaces failed"; return result; }
    // Retain superinterfaces with the rest of the transaction's roots.
    for (jint i = 0; i < interface_count; ++i) {
      jobject next = jni->NewGlobalRef(interfaces[i]);
      jni->DeleteLocalRef(interfaces[i]);
      if (!next) { jni->ExceptionClear(); result = JVMTI_ERROR_OUT_OF_MEMORY; continue; }
      roots->push_back(next);
      pending.push_back(static_cast<jclass>(next));
    }
    env->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
    if (result != JVMTI_ERROR_NONE) { *error = "cannot retain a parent interface"; return result; }
  }
  return JVMTI_ERROR_NONE;
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
  for (const auto& parent : snapshot->live.interfaces) {
    if (std::find(incoming.interfaces.begin(), incoming.interfaces.end(), parent) != incoming.interfaces.end()) continue;
    if (!jni_interface_dispatch::Prepare()) {
      *error = "JNI interface receiver services unavailable in Native snapshot";
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    if (!bound_method_handle::Prepare(jni, env)) {
      if (jni->ExceptionCheck()) jni->ExceptionClear();
      *error = "bound MethodHandle entry unavailable in Native snapshot";
      return JVMTI_ERROR_NOT_AVAILABLE;
    }
    break;
  }
  snapshot->changes_invoke_kind = (snapshot->live.access ^ incoming.access) & evolution::kAccInterface;
  for (const auto& old : snapshot->live.methods)
    for (const auto& next : incoming.methods) {
      if (old.name != next.name || old.descriptor != next.descriptor) continue;
      const uint32_t now = evolution::MethodModifiers(next.access);
      constexpr uint32_t kDirect = evolution::kAccStatic | evolution::kAccPrivate;
      if (((old.access ^ now) & evolution::kAccStatic) || (!(old.access & kDirect) && (now & evolution::kAccPrivate)))
        snapshot->changes_invoke_kind = true;
    }
  // A newly declared private interface method can replace resolution of an
  // inherited symbol in an obsolete self frame. Capture the same Native frame
  // proof as virtual-to-private changes; admission still checks inheritance.
  if (snapshot->live.access & evolution::kAccInterface)
    for (const auto& next : incoming.methods) {
      if (!(next.access & evolution::kAccPrivate) || next.IsStatic()) continue;
      bool declared = false;
      for (const auto& old : snapshot->live.methods)
        declared |= old.name == next.name && old.descriptor == next.descriptor;
      snapshot->changes_invoke_kind |= !declared;
    }
  // A reference field whose type changes keeps a compatible old value: the
  // commit needs the new type's class, resolved here without initializing it.
  for (const auto& old : snapshot->live.fields) {
    if (old.IsStatic() || old.descriptor.front() != 'L') continue;
    for (const auto& next : incoming.fields) {
      if (next.name != old.name || next.IsStatic() != old.IsStatic() || next.descriptor == old.descriptor ||
          next.descriptor.front() != 'L') continue;
      jclass type = nullptr;
      bool same_package = false;
      jvmtiError result = resolver->Resolve(next.descriptor, snapshot->live.descriptor, roots, &type, &same_package, error);
      if (result != JVMTI_ERROR_NONE) return result;
      if (type) snapshot->retyped_fields.push_back({next.name, next.IsStatic(), next.descriptor, type});
    }
  }
  const auto& before = snapshot->live.interfaces;
  std::vector<std::string> added;
  for (const auto& descriptor : incoming.interfaces)
    if (std::find(before.begin(), before.end(), descriptor) == before.end())
      added.push_back(descriptor);
  // Parent changes can invalidate obsolete direct self calls even when the
  // declaration's own flags do not change. Capture those frames before ART
  // enters its structural transaction, as for invocation-kind evolution.
  if ((snapshot->live.access & evolution::kAccInterface) && before != incoming.interfaces)
    for (const auto& method : incoming.methods)
      if (method.access & (evolution::kAccStatic | evolution::kAccPrivate))
        snapshot->changes_invoke_kind = true;
  const bool new_superclass = incoming.superclass != snapshot->live.superclass;
  if (added.empty() && !new_superclass) return JVMTI_ERROR_NONE;
  jvmtiError result = JVMTI_ERROR_NONE;
  std::vector<jclass> parents;
  for (const auto& descriptor : added) {
    jclass resolved = nullptr;
    structural_policy::ResolvedType type;
    result = resolver->Resolve(descriptor, snapshot->live.descriptor, roots, &resolved,
                              &type.same_runtime_package, error);
    if (result != JVMTI_ERROR_NONE) return result;
    if (!resolved) continue;
    snapshot->hierarchy.added_interfaces[descriptor] = type;
    snapshot->interface_classes[descriptor] = resolved;
    parents.push_back(resolved);
  }
  if (snapshot->live.access & evolution::kAccInterface) {
    result = DescribeParentMethods(env, jni, definition.klass, std::move(parents), roots, snapshot, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  if (!new_superclass) return JVMTI_ERROR_NONE;
  structural_policy::NewSuperclass superclass;
  result = resolver->Resolve(incoming.superclass, snapshot->live.descriptor, roots,
                            &snapshot->superclass_class, &superclass.same_runtime_package, error);
  if (result != JVMTI_ERROR_NONE || !snapshot->superclass_class) return result;
  result = DescribeSuperclass(env, jni, definition.klass, snapshot->superclass_class,
                              snapshot->live.access & evolution::kAccInterface, &superclass, error);
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
  snapshot->interface_subtypes = candidates;
  return CollectFacts(env, jni, target, candidates, snapshot, error);
}
}  // namespace artdcevm::hierarchy_snapshot
