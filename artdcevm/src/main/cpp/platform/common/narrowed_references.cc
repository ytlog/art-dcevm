#include "platform/common/narrowed_references.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <tuple>
#include "platform/common/art_dex_view.h"
#include "platform/common/art_method_identity.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/narrowed_reference_symbols.h"

namespace artdcevm::narrowed_references {
namespace {
namespace abi = artdcevm::abi::arm64;
using runtime_thread::VerifiedThread;
using class_layout::Fail;
struct Field { uint32_t offset; size_t signature; };
struct Type {
  jclass root = nullptr;  // Borrowed from the standard entry's loaded array.
  std::string name;
  std::vector<std::string> direct;  // Only for types absent from affected_types.
  const transaction::AffectedType* affected = nullptr;
  std::vector<Field> before, after;
  std::vector<bool> compatible;
  bool modifies_instances = false, proxy = false;
};
struct Key { uint32_t klass; size_t type; bool replacement; };
struct State final : transaction::Part {
  std::vector<Type> types;
  std::vector<Key> keys;  // Sized before the no-failure point; refreshed once while paused.
  abi::RedefinitionDataIter data{};
  int selected = -1;
  bool planned = false, refreshed = false;
};
State* Get() { return transaction::PartOf<State>(transaction::Current(), &transaction::Context::references); }
class_layout::Services services;
using Visitor = void (*)(const void*, void*, uint32_t, bool);
void* original_visit = nullptr;
std::atomic<bool> ready{false}, installed{false};
std::atomic<uint64_t> visits{0}, clears{0};
thread_local State* active = nullptr;

const Key* Find(const State& state, uint32_t klass) {
  auto found = std::lower_bound(state.keys.begin(), state.keys.end(), klass,
      [](const Key& key, uint32_t value) { return key.klass < value; });
  return found != state.keys.end() && found->klass == klass ? &*found : nullptr;
}
void Visit(const void* visitor, void* object, uint32_t offset, bool is_static) noexcept {
  reinterpret_cast<Visitor>(original_visit)(visitor, object, offset, is_static);
  if (!active || is_static || !object) return;
  const Key* key = Find(*active, static_cast<abi::Object*>(object)->klass);
  if (!key) return;
  const auto& type = active->types[key->type];
  const auto& fields = key->replacement ? type.after : type.before;
  auto found = std::lower_bound(fields.begin(), fields.end(), offset,
      [](const Field& field, uint32_t value) { return field.offset < value; });
  if (found == fields.end() || found->offset != offset) return;
  visits.fetch_add(1, std::memory_order_relaxed);
  auto* slot = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object) + offset);
  if (!*slot) return;
  // ART has paused all threads/moving GC; its original visitor just rewrote
  // this reference. No new object is stored, so a null write needs no barrier.
  const auto* value = reinterpret_cast<const abi::Object*>(static_cast<uintptr_t>(*slot));
  const Key* value_type = Find(*active, value->klass);
  if (value_type && active->types[value_type->type].compatible[found->signature]) return;
  *slot = 0;
  clears.fetch_add(1, std::memory_order_relaxed);
}

struct Ops {
  bool (*plan)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, const abi::RedefinitionDataIter&, State&, std::string*);
  void (*refresh)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, State&);
};
template <typename Class, typename Method> struct Planner {
  struct Member { std::string name, signature; bool is_static; uint32_t offset; };
  static Class* Replacement(const VerifiedThread& thread, const abi::RedefinitionDataIter& data, Class* klass) {
    const auto* holder = data.holder->array.Get<abi::ObjectArray>();
    for (int32_t owner = 0; holder && owner < holder->length / abi::RedefinitionDataIter::kNumSlots; ++owner) {
      abi::RedefinitionDataIter item{owner, data.holder};
      const auto* before = item.Classes(true);
      const auto* after = item.Classes(false);
      for (int32_t i = 0; before && after && i < before->length; ++i)
        if (class_layout::Element<Class>(thread, before, i) == klass)
          return class_layout::Element<Class>(thread, after, i);
    }
    return klass;
  }
  static bool Members(const VerifiedThread& thread, Class* klass, std::vector<Member>* fields, std::string* why) {
    const auto* dex = class_layout::DexOf(thread, klass->dex_cache);
    for (uint64_t array : class_layout::View<Class, Method>::FieldArrays(*klass)) {
      auto* members = class_layout::Members<abi::ArtField>(array);
      for (uint32_t i = 0; members && i < members->length; ++i) {
        const auto* field = members->At(i);
        if (!dex) continue;  // Generated Proxy fields have no DEX/instance state.
        evolution::Member identity;
        if (!evolution::ReadFieldIdentity(DexViewOf(*dex), field->dex_field_index, &identity, why)) return false;
        fields->push_back({identity.name, identity.descriptor,
                           (field->access_flags & evolution::kAccStatic) != 0, field->offset});
      }
    }
    const auto order = [](const Member& a, const Member& b) {
      return std::tie(a.name, a.signature, a.is_static, a.offset) < std::tie(b.name, b.signature, b.is_static, b.offset);
    };
    std::sort(fields->begin(), fields->end(), order);
    return true;
  }
  static bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
                   const abi::RedefinitionDataIter& data, State& state, std::string* why) {
    const auto* holder = data.holder->array.Get<abi::ObjectArray>();
    if (!holder || holder->length % abi::RedefinitionDataIter::kNumSlots)
      return Fail(why, "invalid reference-cleanup holder");
    std::vector<Class*> old, final;
    std::map<Class*, size_t> index;
    for (size_t i = 0; i < state.types.size(); ++i) {
      auto* klass = static_cast<Class*>(class_layout::DecodeClass(services.mirror_class, self, state.types[i].root));
      if (!klass) return Fail(why, "missing reference-cleanup class root");
      old.push_back(klass); final.push_back(Replacement(thread, data, klass));
      index[klass] = i; index[final.back()] = i;
    }
    std::set<std::string> removed;
    for (size_t t = 0; t < old.size(); ++t) {
      if (old[t] == final[t]) continue;
      const auto* before = class_layout::Heap<abi::ObjectArray>(thread, old[t]->iftable);
      const auto* after = class_layout::Heap<abi::ObjectArray>(thread, final[t]->iftable);
      for (int32_t i = 0; before && i + 1 < before->length; i += 2) {
        Class* prior = class_layout::Element<Class>(thread, before, i);
        bool kept = false;
        for (int32_t j = 0; after && j + 1 < after->length; j += 2)
          kept |= Replacement(thread, data, class_layout::Element<Class>(thread, after, j)) == Replacement(thread, data, prior);
        if (!kept) removed.insert(class_layout::ClassDescriptor(thread, *prior));
      }
    }
    if (removed.empty()) { state.planned = true; return true; }
    const std::vector<std::string> signatures(removed.begin(), removed.end());
    std::vector<std::vector<std::string>> direct(old.size());
    std::vector<std::vector<Member>> before_fields(old.size()), after_fields(old.size());
    for (size_t t = 0; t < old.size(); ++t) {
      Type& type = state.types[t];
      const auto* dex = class_layout::DexOf(thread, final[t]->dex_cache);
      if (dex && !type.proxy && !type.name.empty() && type.name[0] != '[' && final[t]->dex_class_def_index >= 0) {
        if (!evolution::ReadClassInterfaces(DexViewOf(*dex), final[t]->dex_class_def_index, &direct[t], why))
          return Fail(why, type.name + " direct interfaces: " + *why);
      } else direct[t] = type.affected ? type.affected->interfaces : type.direct;
      // CreateProxyClass gives generated classes Proxy's DexCache but no
      // ClassDef; its two synthetic own fields are static (class_linker.cc).
      // Parent instance fields are still collected below from java.lang.Proxy.
      if (!type.proxy && !Members(thread, old[t], &before_fields[t], why)) return Fail(why, type.name + " old fields: " + *why);
      if (!type.proxy && !Members(thread, final[t], &after_fields[t], why)) return Fail(why, type.name + " new fields: " + *why);
      const auto equal = [](const Member& a, const Member& b) {
        // ART embeds its vtable before static storage, unlike HotSpot's
        // mirror. DEX also loses ClassFile declaration order. A
        // method/interface-only change may move ART static offsets
        // without changing JBR's instance-update flag.
        // JBR explicitly leaves access/storage flag differences as FIXME;
        // the flag itself must not invent an instance-update requirement.
        return std::tie(a.name, a.signature) == std::tie(b.name, b.signature) &&
               ((a.is_static && b.is_static) || a.offset == b.offset);
      };
      type.modifies_instances = !std::equal(before_fields[t].begin(), before_fields[t].end(),
                                            after_fields[t].begin(), after_fields[t].end(), equal) ||
          Replacement(thread, data, class_layout::Heap<Class>(thread, old[t]->super_class)) !=
          class_layout::Heap<Class>(thread, final[t]->super_class);
    }
    // JBR propagates ModifyInstances through the new superclass and local
    // interfaces. IfTable contains that interface closure, so fixed-point
    // propagation gives the same flag without depending on target order.
    for (size_t pass = 0; pass < old.size(); ++pass) {
      bool changed = false;
      for (size_t t = 0; t < old.size(); ++t) {
        if (state.types[t].modifies_instances) continue;
        auto inherit = [&](Class* parent) {
          const auto found = index.find(Replacement(thread, data, parent));
          return found != index.end() && state.types[found->second].modifies_instances;
        };
        bool flag = inherit(class_layout::Heap<Class>(thread, final[t]->super_class));
        const auto* interfaces = class_layout::Heap<abi::ObjectArray>(thread, final[t]->iftable);
        for (int32_t i = 0; !flag && interfaces && i + 1 < interfaces->length; i += 2)
          flag |= inherit(class_layout::Element<Class>(thread, interfaces, i));
        if (flag) { state.types[t].modifies_instances = true; changed = true; }
      }
      if (!changed) break;
    }
    for (size_t t = 0; t < old.size(); ++t) {
      Type& type = state.types[t];
      type.compatible.resize(signatures.size(), false);
      for (Class* c = final[t]; c; c = Replacement(thread, data, class_layout::Heap<Class>(thread, c->super_class))) {
        const auto found = index.find(c);
        if (found == index.end()) return Fail(why, "reference compatibility ancestor missing from snapshot");
        const size_t ancestor = found->second;
        for (size_t s = 0; s < signatures.size(); ++s)
          type.compatible[s] = type.compatible[s] || state.types[ancestor].name == signatures[s] ||
              std::find(direct[ancestor].begin(), direct[ancestor].end(), signatures[s]) != direct[ancestor].end();
      }
      // JBR does not check fields in an affected holder unless it has instance
      // update information. A body/interface-only change preserves those refs.
      if (type.affected && !type.modifies_instances) continue;
      for (bool replacement : {false, true}) {
        auto& fields = replacement ? type.after : type.before;
        for (Class* c = replacement ? final[t] : old[t]; c; c = class_layout::Heap<Class>(thread, c->super_class)) {
          const auto found = index.find(c);
          if (found == index.end()) return Fail(why, "reference field ancestor missing from snapshot");
          const size_t ancestor = found->second;
          const auto& members = replacement ? after_fields[ancestor] : before_fields[ancestor];
          for (const Member& member : members) {
            if (member.is_static) continue;
            const auto signature = std::lower_bound(signatures.begin(), signatures.end(), member.signature);
            if (signature != signatures.end() && *signature == member.signature)
              fields.push_back({member.offset, static_cast<size_t>(signature - signatures.begin())});
          }
        }
        std::sort(fields.begin(), fields.end(), [](const Field& a, const Field& b) { return a.offset < b.offset; });
      }
    }
    state.keys.resize(2 * state.types.size());
    state.data = data;
    state.planned = true;
    return true;
  }
  static void Refresh(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self, State& state) {
    for (size_t t = 0; t < state.types.size(); ++t) {
      auto* old = static_cast<Class*>(class_layout::DecodeClass(services.mirror_class, self, state.types[t].root));
      auto* next = Replacement(thread, state.data, old);
      state.keys[2 * t] = {class_layout::Reference(old), t, old == next};
      state.keys[2 * t + 1] = {class_layout::Reference(next), t, true};
    }
    std::sort(state.keys.begin(), state.keys.end(), [](const Key& a, const Key& b) { return a.klass < b.klass; });
  }
  static constexpr Ops Make(int) { return {Plan, Refresh}; }
};
const auto kOps = class_layout::Table<Planner, Ops>();
}  // namespace

bool Prepare(const class_layout::Services& layout, std::vector<Hook>* hooks, std::string* why) {
  ready.store(false);
  void* address = symbols::Find(symbols::narrowed_references::kVisitReference);
  if (!address || !layout.mirror_class) return Fail(why, "missing ART replacement reference visitor");
  services = layout;
  hooks->push_back({address, HookAddress(Visit), &original_visit, symbols::narrowed_references::kVisitReference.name});
  ready.store(true);
  return true;
}
void Installed(bool value) { installed.store(value); }
bool Available() { return ready.load() && installed.load(); }
jvmtiError Capture(jvmtiEnv* env, JNIEnv* jni, jint count, const jclass* classes, std::string* why) {
  if (!Available()) return JVMTI_ERROR_NONE;
  if (jni->PushLocalFrame(16) != JNI_OK) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *why = "reference snapshot local frame allocation failed";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  struct LocalFrame {
    JNIEnv* jni;
    ~LocalFrame() { jni->PopLocalFrame(nullptr); }
  } frame{jni};
  // Borrowed loaded-class locals and affected global roots predate this frame.
  // Only this capture's temporary Proxy/interface references are popped.
  auto next = std::make_unique<State>();
  jclass proxy = jni->FindClass("java/lang/reflect/Proxy");
  jmethodID is_proxy = proxy ? jni->GetStaticMethodID(proxy, "isProxyClass", "(Ljava/lang/Class;)Z") : nullptr;
  if (!is_proxy) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    if (proxy) jni->DeleteLocalRef(proxy);
    *why = "Proxy classification unavailable for reference snapshot";
    return JVMTI_ERROR_INTERNAL;
  }
  for (jint i = 0; i < count; ++i) {
    jint status = 0;
    if (env->GetClassStatus(classes[i], &status) != JVMTI_ERROR_NONE || !(status & JVMTI_CLASS_STATUS_PREPARED)) continue;
    Type type; type.root = classes[i];
    type.proxy = jni->CallStaticBooleanMethod(proxy, is_proxy, classes[i]);
    if (jni->ExceptionCheck()) {
      jni->ExceptionClear(); jni->DeleteLocalRef(proxy);
      *why = "Proxy classification failed for reference snapshot";
      return JVMTI_ERROR_INTERNAL;
    }
    char* name = nullptr;
    jvmtiError error = env->GetClassSignature(classes[i], &name, nullptr);
    if (error != JVMTI_ERROR_NONE) return error;
    type.name = name; env->Deallocate(reinterpret_cast<unsigned char*>(name));
    for (const auto& affected : transaction::Current()->affected_types)
      if (jni->IsSameObject(affected.klass, classes[i])) { type.affected = &affected; break; }
    if (!type.affected) {
      jint size = 0; jclass* interfaces = nullptr;
      error = env->GetImplementedInterfaces(classes[i], &size, &interfaces);
      if (error != JVMTI_ERROR_NONE) { *why = "cannot capture reference compatibility interfaces"; return error; }
      for (jint j = 0; j < size; ++j) {
        char* signature = nullptr;
        error = env->GetClassSignature(interfaces[j], &signature, nullptr);
        if (error == JVMTI_ERROR_NONE) { type.direct.emplace_back(signature); env->Deallocate(reinterpret_cast<unsigned char*>(signature)); }
        jni->DeleteLocalRef(interfaces[j]);
        if (error != JVMTI_ERROR_NONE) { env->Deallocate(reinterpret_cast<unsigned char*>(interfaces)); return error; }
      }
      env->Deallocate(reinterpret_cast<unsigned char*>(interfaces));
    }
    next->types.push_back(std::move(type));
  }
  jni->DeleteLocalRef(proxy);
  transaction::Current()->references = std::move(next);
  return JVMTI_ERROR_NONE;
}
bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
          const abi::RedefinitionDataIter& data, int selected, std::string* why) {
  State* state = Get();
  if (!state) return Fail(why, "reference compatibility snapshot unavailable");
  if (state->planned) return true;
  state->selected = selected;
  return kOps[selected].plan(thread, self, data, *state, why);
}
UpdateScope::UpdateScope(abi::ClassRedefinitionPrefix* self, const abi::RedefinitionDataIter&) : previous_(active) {
  State* state = Get();
  if (!state || !state->planned || state->keys.empty()) return;
  if (!state->refreshed) {
    if (const auto thread = runtime_thread::Ready()) kOps[state->selected].refresh(*thread, self, *state);
    state->refreshed = true;
  }
  active = state;
}
UpdateScope::~UpdateScope() { active = static_cast<State*>(previous_); }
uint64_t Visits() { return visits.load(); }
uint64_t Clears() { return clears.load(); }
}  // namespace artdcevm::narrowed_references
