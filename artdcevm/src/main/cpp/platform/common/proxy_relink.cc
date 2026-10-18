#include "platform/common/proxy_relink.h"

#include <android/log.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <set>
#include <utility>

#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/thread_prefix.h"
#include "platform/common/art_method_identity.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/imt_sealing.h"
#include "platform/common/transaction_context.h"
#include "platform/symbols/interface_redefinition_symbols.h"

namespace artdcevm::proxy_relink {
namespace {
namespace abi = artdcevm::abi::arm64;
using class_layout::Fail;
using runtime_thread::VerifiedThread;

// gc/accounting/card_table.h, Android 11--17: CardTable::MarkCard stores
// kCardDirty at biased_begin[address >> kCardShift]; Thread's tlsPtr_.card_table
// is that biased begin. ART's write barrier for a reference store.
constexpr size_t kCardShift = 10;
constexpr uint8_t kCardDirty = 0x70;
static_assert(offsetof(abi::ThreadTlsPtrPrefix, exception) - offsetof(abi::ThreadTlsPtrPrefix, card_table) ==
              sizeof(void*));

using DecodeGlobal = void* (*)(JavaVM*, jobject);
struct Services {
  void* (*mirror_class)(abi::ClassRedefinitionPrefix*) = nullptr;
  DecodeGlobal decode_global = nullptr;
};
Services services;
std::atomic<bool> ready{false};

// One interface in the Proxy's final transitive interface graph. Its Class
// root starts on the old definition; ART updates that root during commit.
struct InterfaceEntry {
  jobject interface = nullptr;
  jobject methods = nullptr;
};
struct Table {
  jobject proxy = nullptr;
  jobject iftable = nullptr;
  std::vector<InterfaceEntry> interfaces;
  std::vector<std::pair<void*, void*>> prototypes;
  void** imt = nullptr;
  bool planned = false, published = false;
};
struct State final : transaction::Part {
  JNIEnv* jni = nullptr;
  JavaVM* vm = nullptr;
  std::vector<Table> tables;
  bool planned = false;
  ~State() override {
    for (auto& table : tables) {
      for (jobject reference : {table.proxy, table.iftable})
        if (reference) jni->DeleteGlobalRef(reference);
      for (auto& entry : table.interfaces)
        for (jobject reference : {entry.interface, entry.methods})
          if (reference) jni->DeleteGlobalRef(reference);
    }
  }
};
State* CurrentState() {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::proxies);
}

bool Virtual(uint32_t flags) {
  return !(flags & (evolution::kAccStatic | evolution::kAccPrivate | evolution::kAccConstructor));
}

// ART's write barrier for reference stores into `object`, spanning `bytes`.
void MarkCards(const VerifiedThread& thread, const void* object, size_t bytes) {
  const size_t card_table_offset = runtime_thread::Layout(thread).exception - sizeof(void*);
  auto* card_table = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(runtime_thread::Current(thread)) +
                                                  card_table_offset);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(object);
  for (uintptr_t card = begin >> kCardShift; card <= (begin + bytes - 1) >> kCardShift; ++card)
    card_table[card] = kCardDirty;
}

struct Ops {
  bool (*plan)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, const abi::RedefinitionDataIter&,
               State&, Table&, std::string*);
  void (*apply)(const VerifiedThread&, State&, Table&);
};

template <typename Class, typename Method> struct Relinker {
  // Fills `array` with the IfTable methods of `interface`: each maps to the
  // proxy method of the same name and descriptor, so the invocation handler
  // receives it; one the proxy never had maps to the declaration itself (an
  // abstract one throws AbstractMethodError, a default runs its body), as on
  // a JVM's re-linked proxy class. Proxy methods made from `owner`'s methods
  // are recorded in `prototypes` to point at their new declarations.
  static bool MapMethods(const VerifiedThread& thread, Class* proxy, Class* interface, void* owner,
                         abi::PointerArray* array, std::vector<std::pair<void*, void*>>* prototypes,
                         std::string* why) {
    const std::vector<Method*> methods = Virtuals(interface);
    if (static_cast<size_t>(array->length) != methods.size())
      return Fail(why, "interface method count differs from the prepared IfTable array");
    // Each proxy method's interface method (ArtMethod::data_), by identity.
    std::vector<std::pair<Method*, evolution::Member>> proxied;
    auto* proxy_methods = class_layout::Members<Method>(proxy->methods);
    for (uint32_t i = 0; proxy_methods && i < proxy_methods->length; ++i) {
      Method* method = proxy_methods->At(i);
      if (!Virtual(method->access_flags.load(std::memory_order_relaxed)) || !method->data) continue;
      evolution::Member identity;
      if (!class_layout::MethodIdentity<Class>(thread, static_cast<Method*>(method->data), &identity, why))
        return false;
      proxied.emplace_back(method, std::move(identity));
    }
    for (size_t j = 0; j < methods.size(); ++j) {
      evolution::Member identity;
      if (!class_layout::MethodIdentity<Class>(thread, methods[j], &identity, why)) return false;
      Method* target = methods[j];
      for (const auto& [method, prototype] : proxied) {
        if (prototype.name != identity.name || prototype.descriptor != identity.descriptor) continue;
        target = method;
        auto* declaring = class_layout::Heap<void>(thread, static_cast<Method*>(method->data)->declaring_class);
        if (owner && declaring == owner) prototypes->emplace_back(method, methods[j]);
        break;
      }
      array->elements[j] = reinterpret_cast<uintptr_t>(target);
    }
    return true;
  }
  // Virtual methods in declaration order: the new interface's IfTable order.
  static std::vector<Method*> Virtuals(Class* klass) {
    std::vector<Method*> result;
    auto* methods = class_layout::Members<Method>(klass->methods);
    // modifiers.h, Android 11--17: copied defaults/conflicts are not declared
    // interface methods and do not occupy an IfTable method-array slot.
    constexpr uint32_t kCopied = 0x01000000;
    for (uint32_t i = 0; methods && i < methods->length; ++i) {
      const uint32_t flags = methods->At(i)->access_flags.load(std::memory_order_relaxed);
      if (Virtual(flags) && !(flags & kCopied)) result.push_back(methods->At(i));
    }
    return result;
  }

  static Class* Replacement(const VerifiedThread& thread, const abi::RedefinitionDataIter& data, Class* klass) {
    const auto* holder = data.holder->array.Get<abi::ObjectArray>();
    for (int32_t owner = 0; holder && owner < holder->length / abi::RedefinitionDataIter::kNumSlots; ++owner) {
      const abi::RedefinitionDataIter item{owner, data.holder};
      const auto* before = item.Classes(true);
      const auto* after = item.Classes(false);
      for (int32_t i = 0; before && after && i < before->length; ++i)
        if (class_layout::Element<Class>(thread, before, i) == klass)
          return class_layout::Element<Class>(thread, after, i);
    }
    return klass;
  }
  static bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
                   const abi::RedefinitionDataIter& data, State& state, Table& table, std::string* why) {
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, table.proxy));
    auto* replacement = static_cast<abi::ObjectArray*>(services.decode_global(state.vm, table.iftable));
    if (!proxy || !replacement || replacement->length != 2 * static_cast<int32_t>(table.interfaces.size()))
      return Fail(why, "proxy final IfTable differs from its prepared interface graph");
    for (size_t i = 0; i < table.interfaces.size(); ++i) {
      const auto& entry = table.interfaces[i];
      auto* old_interface = static_cast<Class*>(services.decode_global(state.vm, entry.interface));
      auto* interface = Replacement(thread, data, old_interface);
      auto* array = static_cast<abi::PointerArray*>(services.decode_global(state.vm, entry.methods));
      if (!interface || !array) return Fail(why, "proxy relink input missing");
      if (!MapMethods(thread, proxy, interface, interface != old_interface ? old_interface : nullptr,
                      array, &table.prototypes, why)) return false;
      replacement->elements[2 * i] = class_layout::Reference(interface);
      replacement->elements[2 * i + 1] = class_layout::Reference(array);
    }
    if (replacement->length) MarkCards(thread, replacement->elements, sizeof(uint32_t) * replacement->length);
    if (!imt_sealing::Build(thread, self, proxy, class_layout::Selected(), &table.imt, why)) return false;
    table.planned = true;
    return true;
  }
  // The complete batch has crossed ART's no-failure point and all mutators
  // are suspended. Publish once, regardless of the order of structural owners.
  // Subsequent ART reference passes also visit this rooted replacement table.
  static void Apply(const VerifiedThread& thread, State& state, Table& table) {
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, table.proxy));
    auto* replacement = static_cast<abi::ObjectArray*>(services.decode_global(state.vm, table.iftable));
    proxy->iftable = class_layout::Reference(replacement);
    MarkCards(thread, &proxy->iftable, sizeof(uint32_t));
    for (const auto& [object, prototype] : table.prototypes) {
      auto* method = static_cast<Method*>(object);
      method->dex_method_index = static_cast<Method*>(prototype)->dex_method_index;
      method->data = prototype;
    }
    imt_sealing::Publish(proxy, class_layout::Selected(), table.imt);
    table.published = true;
  }
  static constexpr Ops Make(int) { return {Plan, Apply}; }
};
const auto kRelinkers = class_layout::Table<Relinker, Ops>();
}  // namespace

bool Install(const class_layout::Services& layout, std::string* failure) {
  services.mirror_class = layout.mirror_class;
  services.decode_global =
      reinterpret_cast<DecodeGlobal>(symbols::Find(symbols::interface_redefinition::kDecodeGlobal));
  if (!services.mirror_class || !services.decode_global) {
    *failure = std::string("missing ") + symbols::interface_redefinition::kDecodeGlobal.name;
    return false;
  }
  ready.store(true);
  return true;
}

bool Available() { return ready.load() && imt_sealing::Available(); }

namespace {
// Public reflection over the proxy's loaded types, while Native.
struct Reflection {
  JNIEnv* jni;
  jclass class_type = jni->FindClass("java/lang/Class");
  jclass method_type = jni->FindClass("java/lang/reflect/Method");
  jmethodID interfaces_of = class_type ? jni->GetMethodID(class_type, "getInterfaces", "()[Ljava/lang/Class;") : nullptr;
  jmethodID loader_of = class_type ? jni->GetMethodID(class_type, "getClassLoader", "()Ljava/lang/ClassLoader;") : nullptr;
  jmethodID methods_of = class_type ? jni->GetMethodID(class_type, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;") : nullptr;
  jmethodID for_name = class_type ? jni->GetStaticMethodID(class_type, "forName",
      "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
  jmethodID modifiers_of = method_type ? jni->GetMethodID(method_type, "getModifiers", "()I") : nullptr;
  bool Ready() const { return interfaces_of && loader_of && methods_of && for_name && modifiers_of; }
  bool Failed() {
    if (!jni->ExceptionCheck()) return false;
    jni->ExceptionClear();
    return true;
  }
  // Direct superinterfaces as local references, or false on failure.
  bool Interfaces(jclass klass, std::vector<jclass>* out) {
    auto array = static_cast<jobjectArray>(jni->CallObjectMethod(klass, interfaces_of));
    if (Failed() || !array) return false;
    for (jsize i = 0, n = jni->GetArrayLength(array); i < n; ++i)
      out->push_back(static_cast<jclass>(jni->GetObjectArrayElement(array, i)));
    jni->DeleteLocalRef(array);
    return !Failed();
  }
  // Declared instance methods that are neither static nor private: the
  // IfTable methods of an interface (Relinker::Virtuals).
  bool VirtualCount(jclass klass, jsize* count) {
    auto array = static_cast<jobjectArray>(jni->CallObjectMethod(klass, methods_of));
    if (Failed() || !array) return false;
    *count = 0;
    for (jsize i = 0, n = jni->GetArrayLength(array); i < n; ++i) {
      jobject method = jni->GetObjectArrayElement(array, i);
      const jint modifiers = method ? jni->CallIntMethod(method, modifiers_of) : 0;
      if (method && !(modifiers & (evolution::kAccStatic | evolution::kAccPrivate))) ++*count;
      jni->DeleteLocalRef(method);
      if (Failed()) return false;
    }
    jni->DeleteLocalRef(array);
    return true;
  }
};

bool Contains(JNIEnv* jni, const std::vector<jclass>& classes, jclass klass) {
  for (jclass known : classes)
    if (jni->IsSameObject(known, klass)) return true;
  return false;
}
// Read each target's final declarations once. Reflection is used only for
// unchanged interfaces; an added parent may itself change in the same batch.
struct Definition {
  const Interface* target;
  evolution::ClassSchema incoming;
};
struct Graph {
  Reflection reflection;
  const std::vector<Definition>& definitions;
  std::vector<jclass> visiting, ordered;
  std::string* error;
  jvmtiError failure = JVMTI_ERROR_FAILS_VERIFICATION;
  bool Visit(jclass klass) {
    JNIEnv* jni = reflection.jni;
    if (Contains(jni, ordered, klass)) return true;
    if (Contains(jni, visiting, klass)) {
      const Interface* target = definitions.front().target;
      for (const auto& definition : definitions)
        if (jni->IsSameObject(definition.target->klass, klass)) target = definition.target;
      *error = target->live->descriptor + ": cyclic Proxy interface graph";
      failure = JVMTI_ERROR_CIRCULAR_CLASS_DEFINITION;
      return false;
    }
    visiting.push_back(klass);
    const Definition* definition = nullptr;
    for (const auto& candidate : definitions)
      if (jni->IsSameObject(candidate.target->klass, klass)) definition = &candidate;
    std::vector<jclass> parents;
    if (definition) {
      jobject loader = jni->CallObjectMethod(klass, reflection.loader_of);
      if (reflection.Failed()) return false;
      for (const auto& descriptor : definition->incoming.interfaces) {
        std::string binary = descriptor.substr(1, descriptor.size() - 2);
        std::replace(binary.begin(), binary.end(), '/', '.');
        jstring name = jni->NewStringUTF(binary.c_str());
        auto parent = name ? static_cast<jclass>(jni->CallStaticObjectMethod(
            reflection.class_type, reflection.for_name, name, JNI_FALSE, loader)) : nullptr;
        jni->DeleteLocalRef(name);
        if (reflection.Failed() || !parent) { *error = "Proxy parent cannot be resolved: " + descriptor; return false; }
        parents.push_back(parent);
      }
      jni->DeleteLocalRef(loader);
    } else if (!reflection.Interfaces(klass, &parents)) return false;
    for (jclass parent : parents)
      if (!Visit(parent)) return false;
    visiting.pop_back();
    ordered.push_back(klass);
    return true;
  }
  jsize Count(jclass klass) {
    for (const auto& definition : definitions) {
      if (!reflection.jni->IsSameObject(definition.target->klass, klass)) continue;
      jsize count = 0;
      for (const auto& method : definition.incoming.methods)
        if (Virtual(method.access) && method.name != "<init>" && method.name != "<clinit>") ++count;
      return count;
    }
    jsize count = 0;
    return reflection.VirtualCount(klass, &count) ? count : -1;
  }
};

jvmtiError PrepareTable(JNIEnv* jni, jclass proxy, const std::vector<Definition>& definitions,
                       Table& table, std::string* error) {
  if (jni->PushLocalFrame(64) != JNI_OK) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = "proxy relinking cannot reserve local references";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  struct Frame { JNIEnv* jni; ~Frame() { jni->PopLocalFrame(nullptr); } } frame{jni};
  Graph graph{{jni}, definitions, {}, {}, error};
  if (!graph.reflection.Ready()) { *error = "proxy relinking lacks reflection"; return JVMTI_ERROR_INTERNAL; }
  // Proxy's direct interfaces do not change. Only their transitive final
  // hierarchy changes, including removals and shared or batched additions.
  std::vector<jclass> chain;
  for (jclass klass = proxy; klass; klass = jni->GetSuperclass(klass)) chain.push_back(klass);
  for (auto klass = chain.rbegin(); klass != chain.rend(); ++klass) {
    std::vector<jclass> direct;
    if (!graph.reflection.Interfaces(*klass, &direct)) return JVMTI_ERROR_INTERNAL;
    for (jclass interface : direct)
      if (!graph.Visit(interface)) return graph.failure;
  }
  table.proxy = jni->NewGlobalRef(proxy);
  if (!table.proxy) return JVMTI_ERROR_OUT_OF_MEMORY;
  jclass object_type = jni->FindClass("java/lang/Object");
  if (!object_type) return JVMTI_ERROR_OUT_OF_MEMORY;
  auto array = jni->NewObjectArray(2 * static_cast<jsize>(graph.ordered.size()), object_type, nullptr);
  if (!array) return JVMTI_ERROR_OUT_OF_MEMORY;
  table.iftable = jni->NewGlobalRef(array);
  if (!table.iftable) return JVMTI_ERROR_OUT_OF_MEMORY;
  for (jclass interface : graph.ordered) {
    const jsize count = graph.Count(interface);
    if (count < 0) { *error = "proxy interface methods unreadable"; return JVMTI_ERROR_INTERNAL; }
    // Own the empty entry before allocating roots: a native vector allocation
    // failure must not orphan a newly created global reference.
    table.interfaces.emplace_back();
    auto& entry = table.interfaces.back();
    entry.interface = jni->NewGlobalRef(interface);
    if (!entry.interface) return JVMTI_ERROR_OUT_OF_MEMORY;
    auto methods = jni->NewLongArray(count);
    if (!methods) return JVMTI_ERROR_OUT_OF_MEMORY;
    entry.methods = jni->NewGlobalRef(methods);
    if (!entry.methods) return JVMTI_ERROR_OUT_OF_MEMORY;
    jni->DeleteLocalRef(methods);
  }
  return JVMTI_ERROR_NONE;
}
}  // namespace

jvmtiError Prepare(JNIEnv* jni, const std::vector<jclass>& proxies, const std::vector<Interface>& interfaces,
                   std::string* error) {
  transaction::Context* context = transaction::Current();
  if (proxies.empty() || !context) return JVMTI_ERROR_NONE;
  if (!Available()) {
    *error = "interface redefinition rejects Proxy implementations: proxy relinking unavailable";
    return JVMTI_ERROR_UNMODIFIABLE_CLASS;
  }
  auto owned = std::make_unique<State>();
  State& state = *owned;
  state.jni = jni;
  context->proxies = std::move(owned);
  if (jni->GetJavaVM(&state.vm) != JNI_OK) {
    *error = "proxy relinking has no JavaVM";
    return JVMTI_ERROR_INTERNAL;
  }
  std::vector<Definition> definitions;
  for (const Interface& target : interfaces) {
    Definition definition{&target, {}};
    const auto* bytes = target.definition;
    const evolution::DexView view{bytes->class_bytes, static_cast<uint32_t>(bytes->class_byte_count),
                                  bytes->class_bytes, static_cast<uint32_t>(bytes->class_byte_count)};
    if (!evolution::ReadClassDeclarations(view, &definition.incoming, error)) return JVMTI_ERROR_INVALID_CLASS_FORMAT;
    definitions.push_back(std::move(definition));
  }
  for (jclass proxy : proxies) {
    bool affected = false;
    for (const Interface& target : interfaces) affected |= jni->IsAssignableFrom(proxy, target.klass);
    if (jni->ExceptionCheck()) { jni->ExceptionClear(); *error = "proxy assignability failed"; return JVMTI_ERROR_INTERNAL; }
    if (!affected) continue;
    state.tables.emplace_back();
    const jvmtiError result = PrepareTable(jni, proxy, definitions, state.tables.back(), error);
    if (result != JVMTI_ERROR_NONE) {
      if (jni->ExceptionCheck()) jni->ExceptionClear();
      if (error->empty()) *error = "proxy final IfTable preparation failed";
      return result;
    }
  }
  return JVMTI_ERROR_NONE;
}

bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
          const abi::RedefinitionDataIter& data, int selected, std::string* why) {
  State* state = CurrentState();
  if (!state) return true;
  if (state->planned) return true;
  for (Table& table : state->tables)
    if (!kRelinkers[selected].plan(thread, self, data, *state, table, why)) return false;
  state->planned = true;
  return true;
}

void* FinalIfTable(void* proxy) {
  State* state = CurrentState();
  if (!state || !state->planned) return nullptr;
  for (const Table& table : state->tables)
    if (services.decode_global(state->vm, table.proxy) == proxy)
      return services.decode_global(state->vm, table.iftable);
  return nullptr;
}

void Apply(abi::ClassRedefinitionPrefix*, const abi::RedefinitionDataIter&) noexcept {
  State* state = CurrentState();
  const int selected = class_layout::Selected();
  const auto thread = runtime_thread::Ready();
  if (!state || !state->planned || selected < 0 || !thread) return;
  for (auto& table : state->tables)
    if (!table.published) kRelinkers[selected].apply(*thread, *state, table);
}
}  // namespace artdcevm::proxy_relink
