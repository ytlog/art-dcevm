#include "platform/common/interface_redefinition_internal.h"
#include "platform/common/proxy_relink.h"

#include <algorithm>
#include <iterator>
#include <optional>

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
    if (!klass || env->GetClassModifiers(klass, &access) != JVMTI_ERROR_NONE) continue;
    if (!(access & evolution::kAccInterface)) {
      next->class_targets.push_back({klass, false, 0, {}, 0});
      continue;
    }
    if (env->GetClassLoader(klass, &loader) != JVMTI_ERROR_NONE) continue;
    const auto status_result = env->GetClassStatus(klass, &status);
    const bool boot = !loader;
    if (loader) jni->DeleteLocalRef(loader);
    if (status_result != JVMTI_ERROR_NONE) continue;
    next->targets.push_back({klass, boot, status, {}, 0});
  }
  bool has_class_dex = false;
  for (jint i = 0; i < count; ++i)
    has_class_dex |= definitions[i].class_bytes && definitions[i].class_byte_count > 0;
  if ((!next->targets.empty() || (RuntimeAvailable() && count > 1 && has_class_dex)) &&
      jni->EnsureLocalCapacity(8) == JNI_OK) {
    if (jni->GetJavaVM(&next->vm) == JNI_OK)
      next->class_class = jni->FindClass("java/lang/Class");
    if (next->vm) next->object_class = jni->FindClass("java/lang/Object");
    // Resolve Class[] before the subclass-load watch starts.
    jobjectArray array = next->class_class ? jni->NewObjectArray(0, next->class_class, nullptr) : nullptr;
    if (array) jni->DeleteLocalRef(array);
    if (jni->ExceptionCheck()) jni->ExceptionClear();
  }
}
const std::vector<jclass>* RuntimeBackend::GroupTypes(jclass klass) {
  const State* current = CurrentState();
  if (!current) return nullptr;
  for (const auto& target : current->targets)
    if (current->jni->IsSameObject(target.klass, klass) && target.group < current->groups.size())
      return &current->groups[target.group].Current().snapshot.types;
  return nullptr;
}
void RuntimeBackend::Describe(jclass klass, const evolution::ClassSchema& live) {
  State* next = CurrentState();
  if (!next) return;
  for (auto* targets : {&next->targets, &next->class_targets})
    for (auto& target : *targets)
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

namespace {
// Union-find over rebuild candidates: interface roots, then batch ancestors.
struct Sets {
  std::vector<size_t> parent;
  size_t Find(size_t item) {
    while (parent[item] != item) item = parent[item] = parent[parent[item]];
    return item;
  }
  void Join(size_t left, size_t right) {
    left = Find(left), right = Find(right);
    if (left != right) parent[std::max(left, right)] = std::min(left, right);
  }
};
// One order per candidate owner. ART collects only instances of the owner
// entry's class; every other root whose types have instances is a further
// collection round. Owners that cover every concrete member come first.
jvmtiError BuildOrderings(JNIEnv* jni, jvmtiEnv* env, const jclass* loaded, const std::vector<jint>& positions,
                          const std::vector<jclass>& candidates, bool class_roots, Members* members,
                          std::string* error) {
  std::vector<bool> concrete;
  for (jclass type : members->snapshot.types) {
    jint access = 0;
    const jvmtiError result = env->GetClassModifiers(type, &access);
    if (result != JVMTI_ERROR_NONE) { *error = "interface snapshot member modifiers unavailable"; return result; }
    concrete.push_back(!(access & (evolution::kAccInterface | evolution::kAccAbstract)));
  }
  std::vector<Ordering> covering, partial;
  for (jclass candidate : candidates) {
    Ordering ordering{candidate, {}, {candidate}, {}, {}, class_roots};
    for (jclass other : candidates)
      if (other != candidate) ordering.roots.push_back(other);
    if (candidate == candidates.front()) {
      ordering.snapshot = members->snapshot;
    } else {
      const jvmtiError result = OrderTypes(jni, env, candidate, loaded, positions, &ordering.snapshot, error);
      if (result != JVMTI_ERROR_NONE) return result;
    }
    if (class_roots) {
      // Every class whose superclass is outside the rebuild; ordered types
      // put each superclass first, so a class's root is already listed.
      ordering.roots.clear();
      for (size_t i = 0; i < ordering.snapshot.types.size(); ++i) {
        jint access = 0;
        const jvmtiError result = env->GetClassModifiers(ordering.snapshot.types[i], &access);
        if (result != JVMTI_ERROR_NONE) { *error = "interface snapshot member modifiers unavailable"; return result; }
        int32_t first = -1;
        if (!(access & evolution::kAccInterface)) {
          const int32_t parent = ordering.snapshot.superclass[i];
          if (parent < 0) {
            first = static_cast<int32_t>(ordering.roots.size());
            ordering.roots.push_back(ordering.snapshot.types[i]);
          } else {
            first = ordering.first_root[parent];
          }
        }
        ordering.first_root.push_back(first);
        for (size_t k = 0; k < members->snapshot.types.size(); ++k)
          if (members->snapshot.types[k] == ordering.snapshot.types[i] && concrete[k] && first >= 0 &&
              std::find(ordering.rounds.begin(), ordering.rounds.end(), first) == ordering.rounds.end())
            ordering.rounds.push_back(first);
      }
      partial.push_back(std::move(ordering));
      continue;
    }
    std::vector<bool> collected(ordering.roots.size());
    for (jclass type : ordering.snapshot.types) {
      int32_t first = -1;
      for (size_t k = 0; first < 0 && k < ordering.roots.size(); ++k)
        if (jni->IsAssignableFrom(type, ordering.roots[k])) first = static_cast<int32_t>(k);
      if (jni->ExceptionCheck() || first < 0) {
        jni->ExceptionClear();
        *error = "interface rebuild root check failed";
        return JVMTI_ERROR_INTERNAL;
      }
      ordering.first_root.push_back(first);
      // Positions in the shared snapshot, which lists the same types.
      for (size_t i = 0; i < members->snapshot.types.size(); ++i)
        if (members->snapshot.types[i] == type && concrete[i]) collected[first] = true;
    }
    for (size_t k = 1; k < collected.size(); ++k)
      if (collected[k]) ordering.rounds.push_back(static_cast<int32_t>(k));
    (ordering.rounds.empty() ? covering : partial).push_back(std::move(ordering));
  }
  members->orderings = std::move(covering);
  for (auto& ordering : partial) members->orderings.push_back(std::move(ordering));
  return JVMTI_ERROR_NONE;
}
std::vector<jint> Union(std::vector<jint> left, const std::vector<jint>& right) {
  std::vector<jint> merged;
  merged.reserve(left.size() + right.size());
  std::set_union(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(merged));
  return merged;
}
}  // namespace

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
  JNIEnv* jni = next->jni;
  const auto failed = [&](std::string reason) {
    jni->ExceptionClear();
    *error = std::move(reason);
    return JVMTI_ERROR_INTERNAL;
  };
  // A batch child interface belongs to its highest batch ancestor; only roots
  // select subtypes, which already contain every child's subtypes.
  std::vector<size_t> owners(next->targets.size());
  std::vector<size_t> roots;
  for (size_t i = 0; i < next->targets.size(); ++i) {
    owners[i] = i;
    for (size_t j = 0; j < next->targets.size(); ++j)
      if (jni->IsAssignableFrom(next->targets[owners[i]].klass, next->targets[j].klass)) owners[i] = j;
    if (jni->ExceptionCheck())
      return failed(next->targets[i].live.descriptor + ": interface batch ownership check failed");
    if (owners[i] == i) roots.push_back(i);
  }
  // Roots whose subtypes intersect share one rebuild: ART must replace each
  // type once. SelectTypes positions index the exact GetLoadedClasses
  // references, so identity needs no pairwise type comparisons.
  std::vector<std::vector<jint>> selected(roots.size());
  Sets sets;
  std::unordered_map<jint, size_t> claimed;
  std::vector<jint> proxies;
  for (size_t r = 0; r < roots.size(); ++r) {
    sets.parent.push_back(r);
    jvmtiError result = SelectTypes(jni, next->env, next->targets[roots[r]].klass, count, loaded,
                                    &selected[r], error, &proxies);
    if (result != JVMTI_ERROR_NONE) return result;
    for (jint position : selected[r]) {
      const auto [found, inserted] = claimed.emplace(position, r);
      if (!inserted) sets.Join(found->second, r);
    }
  }
  // A batch class outside every root's type relation can still be a
  // superclass of a selected type; ART would rebuild its subtree too.
  std::vector<jclass> ancestors;
  for (jint i = 0; i < next->batch_size; ++i) {
    jclass other = next->definitions[i].klass;
    bool related = false;
    for (const auto& target : next->targets) related |= jni->IsSameObject(target.klass, other);
    for (size_t r = 0; !related && r < roots.size(); ++r)
      related = jni->IsAssignableFrom(other, next->targets[roots[r]].klass);
    if (jni->ExceptionCheck()) return failed("interface batch group-overlap check failed");
    if (related) continue;
    std::optional<size_t> item;
    for (size_t r = 0; r < roots.size(); ++r) {
      for (jint position : selected[r]) {
        if (!jni->IsAssignableFrom(loaded[position], other)) continue;
        if (!item) {
          item = sets.parent.size();
          sets.parent.push_back(*item);
          ancestors.push_back(other);
        }
        sets.Join(*item, r);
        break;
      }
      if (jni->ExceptionCheck()) return failed("interface batch group-overlap check failed");
    }
  }
  std::unordered_map<size_t, size_t> group_of;
  for (size_t r = 0; r < roots.size(); ++r) {
    const size_t set = sets.Find(r);
    auto [found, inserted] = group_of.emplace(set, next->groups.size());
    if (inserted) next->groups.push_back(Group{roots[r], {}, {}, {}, std::nullopt, -1});
    next->targets[roots[r]].group = found->second;
  }
  std::vector<std::vector<jint>> core(next->groups.size()), extended(next->groups.size());
  for (size_t r = 0; r < roots.size(); ++r) {
    const size_t group = next->targets[roots[r]].group;
    core[group] = Union(std::move(core[group]), selected[r]);
  }
  for (size_t i = 0; i < next->targets.size(); ++i) next->targets[i].group = next->targets[owners[i]].group;
  for (size_t a = 0; a < ancestors.size(); ++a) {
    const size_t group = group_of.at(sets.Find(roots.size() + a));
    std::vector<jint> subtree;
    jvmtiError result = SelectTypes(jni, next->env, ancestors[a], count, loaded, &subtree, error);
    if (result != JVMTI_ERROR_NONE) return result;
    if (next->groups[group].ancestors.empty()) extended[group] = core[group];
    next->groups[group].ancestors.push_back(ancestors[a]);
    extended[group] = Union(std::move(extended[group]), subtree);
  }
  if (!proxies.empty()) {
    std::vector<jclass> proxy_classes;
    for (jint position : proxies) proxy_classes.push_back(loaded[position]);
    std::vector<proxy_relink::Interface> interfaces;
    for (const Target& target : next->targets)
      for (jint i = 0; i < next->batch_size; ++i)
        if (jni->IsSameObject(next->definitions[i].klass, target.klass))
          interfaces.push_back({target.klass, &target.live, &next->definitions[i]});
    jvmtiError result = proxy_relink::Prepare(jni, proxy_classes, interfaces, error);
    if (result != JVMTI_ERROR_NONE) return result;
  }
  bool indexed_jni_ids = false;
  for (size_t g = 0; g < next->groups.size(); ++g) {
    Group& group = next->groups[g];
    const Target& leader = next->targets[group.leader];
    std::vector<jclass> candidates;
    for (size_t root : roots)
      if (next->targets[root].group == g) candidates.push_back(next->targets[root].klass);
    jvmtiError result = OrderTypes(jni, next->env, leader.klass, loaded, core[g], &group.core.snapshot, error);
    if (result == JVMTI_ERROR_NONE)
      result = BuildOrderings(jni, next->env, loaded, core[g], candidates, !proxies.empty(), &group.core, error);
    if (result == JVMTI_ERROR_NONE && !group.ancestors.empty()) {
      result = OrderTypes(jni, next->env, leader.klass, loaded, extended[g], &group.extended.snapshot, error);
      candidates.insert(candidates.end(), group.ancestors.begin(), group.ancestors.end());
      if (result == JVMTI_ERROR_NONE)
        result = BuildOrderings(jni, next->env, loaded, extended[g], candidates, !proxies.empty(), &group.extended,
                                error);
    }
    if (result != JVMTI_ERROR_NONE) return result;
    if (!leader.boot) {
      result = VerifyJniIdMode(next->env, group.core.snapshot, &jvmtiEnv::GetClassMethods,
                               &indexed_jni_ids, error);
      if (result == JVMTI_ERROR_NONE && !indexed_jni_ids)
        result = VerifyJniIdMode(next->env, group.core.snapshot, &jvmtiEnv::GetClassFields,
                                 &indexed_jni_ids, error);
      if (result != JVMTI_ERROR_NONE) return result;
    }
    timing.types += group.core.snapshot.types.size();
  }
  // Ordinary structural roots use the same private arrays and ownership as
  // interface rebuilds. The holder later selects only initial structural roots
  // not already absorbed by a selected interface group.
  bool changed_hierarchy = !next->targets.empty();
  for (const auto& target : next->class_targets) {
    for (jint i = 0; i < next->batch_size; ++i) {
      if (!jni->IsSameObject(target.klass, next->definitions[i].klass)) continue;
      const auto& definition = next->definitions[i];
      if (!definition.class_bytes || definition.class_byte_count <= 0) continue;
      evolution::ClassSchema incoming;
      std::string ignored;
      const auto size = static_cast<size_t>(definition.class_byte_count);
      if (evolution::ReadClassDeclarations({definition.class_bytes, size, definition.class_bytes, size},
                                           &incoming, &ignored))
        changed_hierarchy |= incoming.superclass != target.live.superclass ||
                             incoming.interfaces != target.live.interfaces;
    }
  }
  if (RuntimeAvailable() && next->batch_size > 1 && changed_hierarchy) for (const Target& target : next->class_targets) {
    Group group{};
    group.class_root = target.klass;
    std::vector<jint> subtree;
    jvmtiError result = SelectTypes(jni, next->env, target.klass, count, loaded, &subtree, error);
    if (result == JVMTI_ERROR_NONE)
      result = OrderTypes(jni, next->env, target.klass, loaded, subtree, &group.core.snapshot, error);
    if (result == JVMTI_ERROR_NONE)
      result = BuildOrderings(jni, next->env, loaded, subtree, {target.klass}, false, &group.core, error);
    if (result != JVMTI_ERROR_NONE) return result;
    next->groups.push_back(std::move(group));
  }
  if (!next->groups.empty() && jni->EnsureLocalCapacity(8) != JNI_OK) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = "interface snapshot local reference reservation failed";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  for (auto& group : next->groups) {
    const std::string descriptor = group.class_root ? group.core.snapshot.descriptors.front()
                                                       : next->targets[group.leader].live.descriptor;
    for (Members* members : {&group.core, &group.extended}) {
      if (members->snapshot.types.empty()) continue;
      if (!next->vm || !next->class_class) {
        *error = descriptor + ": interface private array JNI services unavailable";
        return JVMTI_ERROR_NOT_AVAILABLE;
      }
      if (members->snapshot.types.size() > static_cast<size_t>(std::numeric_limits<jsize>::max())) {
        *error = descriptor + ": interface private arrays are too large";
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
      members->arrays = ClassArrayRoots::Create(jni, next->vm, next->class_class,
                                                static_cast<jsize>(members->snapshot.types.size()));
      if (!members->arrays) {
        *error = descriptor + ": interface private array allocation failed while Native";
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
    }
  }
  // RetransformClasses can reach Redefiner::Run without ART's earlier normal
  // admission. Probe through public JVMTI while still Native: its fixed ART
  // implementation supplies the genuine StackHandleScope<1> to Admit, before
  // Run needs transaction-lived roots. Never synthesize an unverified scope.
  if (!next->runtime && !next->groups.empty() && !next->class_targets.empty()) {
    jboolean modifiable = JNI_FALSE;
    const auto result = next->env->IsModifiableClass(next->class_targets.front().klass, &modifiable);
    if (result != JVMTI_ERROR_NONE) { *error = "batch root admission probe failed"; return result; }
    if (!next->runtime_verified) { *error = "batch root declarations were not verified by admission"; return JVMTI_ERROR_NOT_AVAILABLE; }
  }
  if (!next->groups.empty())
    timing.snapshot_ns = loaded_ns + std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
  return JVMTI_ERROR_NONE;
}

}  // namespace artdcevm::interface_redefinition
