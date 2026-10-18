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

// One proxy class and one interface target it implements.
struct Entry {
  jobject proxy = nullptr;      // Global references, released with the transaction.
  jobject interface = nullptr;
  jobject methods = nullptr;    // long[]: the new IfTable method array.
  int32_t index = -1;            // The structural owner that commits this group.
  int32_t class_index = -1;      // This interface in the owner's class arrays.
  int32_t iftable_slot = -1;     // IfTable pair index of the interface.
  std::vector<std::pair<void*, void*>> prototypes;  // Proxy method, new interface method.
};
// A parent interface that an interface target adds and the proxy class does
// not implement yet: it gains an IfTable entry, as a re-linked proxy would.
struct Inserted {
  jobject interface = nullptr;  // Global references, released with the transaction.
  jobject methods = nullptr;    // long[]: its IfTable method array.
};
struct Table {
  jobject proxy = nullptr;
  void** imt = nullptr;
  bool built = false, published = false;
  // Added parents, superinterfaces first, inserted before the IfTable pair of
  // `anchor` (a target interface that added them) into a new IfTable sized
  // `pairs` + inserted.size() pairs. Empty when no target adds parents.
  std::vector<Inserted> inserted;
  jobject anchor = nullptr;
  jobject iftable = nullptr;    // Object[]: the new IfTable.
  int32_t pairs = 0;            // IfTable pairs the proxy class has now.
  int32_t before = -1;          // Pair index of `anchor` in the old IfTable.
  int32_t owner = -1;           // The structural owner committing `anchor`.
  bool planned = false, swapped = false;
};
struct State final : transaction::Part {
  JNIEnv* jni = nullptr;
  JavaVM* vm = nullptr;
  std::vector<Entry> entries;
  std::vector<Table> tables;
  ~State() override {
    for (auto& entry : entries)
      for (jobject reference : {entry.proxy, entry.interface, entry.methods})
        if (reference) jni->DeleteGlobalRef(reference);
    for (auto& table : tables) {
      for (jobject reference : {table.proxy, table.anchor, table.iftable})
        if (reference) jni->DeleteGlobalRef(reference);
      for (auto& parent : table.inserted)
        for (jobject reference : {parent.interface, parent.methods})
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
               State&, Entry&, std::string*);
  void (*apply)(const VerifiedThread&, State&, Entry&, void* new_interface);
  bool (*plan_parents)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, State&, Table&, std::string*);
  void (*swap)(const VerifiedThread&, State&, Table&);
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
    for (uint32_t i = 0; methods && i < methods->length; ++i)
      if (Virtual(methods->At(i)->access_flags.load(std::memory_order_relaxed))) result.push_back(methods->At(i));
    return result;
  }

  static bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
                   const abi::RedefinitionDataIter& data, State& state, Entry& entry, std::string* why) {
    auto* old_interface = class_layout::Element<Class>(thread, data.Classes(true), entry.class_index);
    auto* new_interface = class_layout::Element<Class>(thread, data.Classes(false), entry.class_index);
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, entry.proxy));
    auto* array = static_cast<abi::PointerArray*>(services.decode_global(state.vm, entry.methods));
    if (!old_interface || !new_interface || !proxy || !array) return Fail(why, "proxy relink input missing");
    // The proxy's IfTable must name the old interface exactly once.
    auto* iftable = class_layout::Heap<abi::ObjectArray>(thread, proxy->iftable);
    entry.iftable_slot = -1;
    for (int32_t i = 0; iftable && 2 * i + 1 < iftable->length; ++i) {
      if (class_layout::Heap<void>(thread, iftable->elements[2 * i]) != old_interface) continue;
      if (entry.iftable_slot >= 0) return Fail(why, "proxy IfTable names the interface twice");
      entry.iftable_slot = i;
    }
    if (entry.iftable_slot < 0) return Fail(why, "proxy IfTable does not name the interface");
    entry.prototypes.clear();
    if (!MapMethods(thread, proxy, new_interface, old_interface, array, &entry.prototypes, why)) return false;
    for (auto& table : state.tables) {
      if (table.built || services.decode_global(state.vm, table.proxy) != proxy) continue;
      if (!imt_sealing::Build(thread, self, proxy, class_layout::Selected(), &table.imt, why)) return false;
      table.built = true;
    }
    entry.index = data.index;
    return true;
  }

  static void Apply(const VerifiedThread& thread, State& state, Entry& entry, void* new_interface) {
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, entry.proxy));
    auto* array = services.decode_global(state.vm, entry.methods);
    // Another owner of the batch may already have swapped in a longer IfTable.
    auto* iftable = class_layout::Heap<abi::ObjectArray>(thread, proxy->iftable);
    int32_t slot = -1;
    for (int32_t i = 0; iftable && 2 * i + 1 < iftable->length && slot < 0; ++i)
      if (class_layout::Heap<void>(thread, iftable->elements[2 * i]) == new_interface) slot = i;
    if (slot < 0) {
      __android_log_write(ANDROID_LOG_FATAL, "ART-DCEVM", "proxy IfTable does not name the new interface");
      return;
    }
    iftable->elements[2 * slot + 1] = class_layout::Reference(array);
    MarkCards(thread, &iftable->elements[2 * slot + 1], sizeof(uint32_t));
    for (const auto& [object, prototype] : entry.prototypes) {
      auto* method = static_cast<Method*>(object);
      method->dex_method_index = static_cast<Method*>(prototype)->dex_method_index;
      method->data = prototype;
    }
  }
  // Before ART's no-failure point: check the IfTable Native counted, find the
  // anchor's pair and fill the added parents' method arrays.
  static bool PlanParents(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self, State& state,
                          Table& table, std::string* why) {
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, table.proxy));
    auto* iftable = proxy ? class_layout::Heap<abi::ObjectArray>(thread, proxy->iftable) : nullptr;
    void* anchor = class_layout::DecodeClass(services.mirror_class, self, static_cast<jclass>(table.anchor));
    auto* replacement = static_cast<abi::ObjectArray*>(services.decode_global(state.vm, table.iftable));
    if (!iftable || !anchor || !replacement) return Fail(why, "proxy parent relink input missing");
    if (iftable->length != 2 * table.pairs ||
        replacement->length != 2 * (table.pairs + static_cast<int32_t>(table.inserted.size())))
      return Fail(why, "proxy IfTable differs from its loaded interfaces");
    table.before = -1;
    for (int32_t i = 0; i < table.pairs && table.before < 0; ++i)
      if (class_layout::Heap<void>(thread, iftable->elements[2 * i]) == anchor) table.before = i;
    if (table.before < 0) return Fail(why, "proxy IfTable does not name the interface adding parents");
    for (auto& parent : table.inserted) {
      auto* interface = static_cast<Class*>(services.decode_global(state.vm, parent.interface));
      auto* array = static_cast<abi::PointerArray*>(services.decode_global(state.vm, parent.methods));
      if (!interface || !array) return Fail(why, "proxy parent relink input missing");
      if (!MapMethods(thread, proxy, interface, nullptr, array, nullptr, why)) return false;
    }
    return true;
  }
  // At commit, every thread suspended: copy the IfTable with the added
  // parents before the anchor's pair (superinterfaces precede the interfaces
  // that extend them) and publish it. Lookups by IfTable then see the new
  // parents; the sealed IMT sends every interface call to that lookup.
  static void Swap(const VerifiedThread& thread, State& state, Table& table) {
    auto* proxy = static_cast<Class*>(services.decode_global(state.vm, table.proxy));
    auto* iftable = class_layout::Heap<abi::ObjectArray>(thread, proxy->iftable);
    auto* replacement = static_cast<abi::ObjectArray*>(services.decode_global(state.vm, table.iftable));
    if (!iftable || !replacement || iftable->length != 2 * table.pairs) {
      __android_log_write(ANDROID_LOG_FATAL, "ART-DCEVM", "proxy IfTable changed before its parents were added");
      return;
    }
    int32_t out = 0;
    for (int32_t i = 0; i <= table.pairs; ++i) {
      if (i == table.before) {
        for (auto& parent : table.inserted) {
          replacement->elements[out++] = class_layout::Reference(services.decode_global(state.vm, parent.interface));
          replacement->elements[out++] = class_layout::Reference(services.decode_global(state.vm, parent.methods));
        }
      }
      if (i == table.pairs) break;
      replacement->elements[out++] = iftable->elements[2 * i];
      replacement->elements[out++] = iftable->elements[2 * i + 1];
    }
    MarkCards(thread, replacement->elements, sizeof(uint32_t) * replacement->length);
    proxy->iftable = class_layout::Reference(replacement);
    MarkCards(thread, &proxy->iftable, sizeof(uint32_t));
    table.swapped = true;
  }
  static constexpr Ops Make(int) { return {Plan, Apply, PlanParents, Swap}; }
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
// Interfaces of `klass` and its superclasses, superinterfaces first: the
// order of an IfTable.
bool Closure(Reflection& reflection, jclass klass, std::vector<jclass>* out) {
  std::vector<jclass> direct;
  if (!reflection.Interfaces(klass, &direct)) return false;
  for (jclass interface : direct) {
    if (!Contains(reflection.jni, *out, interface)) {
      if (!Closure(reflection, interface, out)) return false;
      if (!Contains(reflection.jni, *out, interface)) out->push_back(interface);
      else reflection.jni->DeleteLocalRef(interface);
    } else {
      reflection.jni->DeleteLocalRef(interface);
    }
  }
  return true;
}

// The interfaces that `target`'s added parents bring and the proxy does not
// implement yet, with a method array each and the new IfTable.
jvmtiError CollectParents(JNIEnv* jni, jclass proxy, const Interface& target, const std::vector<std::string>& added,
                          const std::vector<Interface>& batch, Table& table, std::string* error) {
  const auto fail = [&](jvmtiError code, std::string reason) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    *error = target.live->descriptor + ": " + reason;
    return code;
  };
  if (!table.inserted.empty() || table.anchor)
    return fail(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                "a Proxy implementation cannot follow parent additions of two interfaces in one batch");
  Reflection reflection{jni};
  if (!reflection.Ready()) return fail(JVMTI_ERROR_INTERNAL, "proxy parent relinking lacks reflection");
  jobject loader = jni->CallObjectMethod(target.klass, reflection.loader_of);
  if (reflection.Failed()) return fail(JVMTI_ERROR_INTERNAL, "proxy parent relinking has no class loader");
  // The proxy's current IfTable: interfaces of the proxy class and its superclasses.
  std::vector<jclass> existing;
  for (jclass klass = static_cast<jclass>(jni->NewLocalRef(proxy)); klass;) {
    if (!Closure(reflection, klass, &existing)) return fail(JVMTI_ERROR_INTERNAL, "proxy interfaces unreadable");
    jclass super = jni->GetSuperclass(klass);
    jni->DeleteLocalRef(klass);
    klass = super;
  }
  std::vector<jclass> parents;
  for (const std::string& descriptor : added) {
    std::string binary = descriptor.size() > 2 ? descriptor.substr(1, descriptor.size() - 2) : descriptor;
    std::replace(binary.begin(), binary.end(), '/', '.');
    jstring name = jni->NewStringUTF(binary.c_str());
    auto parent = name ? static_cast<jclass>(jni->CallStaticObjectMethod(
        reflection.class_type, reflection.for_name, name, JNI_FALSE, loader)) : nullptr;
    jni->DeleteLocalRef(name);
    if (reflection.Failed() || !parent)
      return fail(JVMTI_ERROR_FAILS_VERIFICATION, "added parent interface cannot be resolved: " + descriptor);
    std::vector<jclass> closure;
    if (!Closure(reflection, parent, &closure)) return fail(JVMTI_ERROR_INTERNAL, "parent interfaces unreadable");
    closure.push_back(parent);
    for (jclass interface : closure) {
      if (Contains(jni, existing, interface) || Contains(jni, parents, interface)) continue;
      // A parent the batch also redefines would need its replacement Class,
      // which ART publishes only as this owner commits.
      for (const Interface& other : batch)
        if (jni->IsSameObject(other.klass, interface))
          return fail(JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,
                      "a Proxy implementation cannot gain a parent interface the batch also redefines");
      parents.push_back(interface);
    }
  }
  if (parents.empty()) return JVMTI_ERROR_NONE;
  table.anchor = jni->NewGlobalRef(target.klass);
  table.pairs = static_cast<int32_t>(existing.size());
  jclass object_type = jni->FindClass("java/lang/Object");
  jobjectArray iftable = object_type ? jni->NewObjectArray(
      2 * static_cast<jsize>(existing.size() + parents.size()), object_type, nullptr) : nullptr;
  table.iftable = iftable ? jni->NewGlobalRef(iftable) : nullptr;
  if (!table.anchor || !table.iftable) return fail(JVMTI_ERROR_OUT_OF_MEMORY, "proxy IfTable allocation failed");
  for (jclass parent : parents) {
    jsize count = 0;
    if (!reflection.VirtualCount(parent, &count)) return fail(JVMTI_ERROR_INTERNAL, "parent methods unreadable");
    jlongArray methods = jni->NewLongArray(count);
    Inserted inserted{jni->NewGlobalRef(parent), methods ? jni->NewGlobalRef(methods) : nullptr};
    table.inserted.push_back(inserted);
    if (!inserted.interface || !inserted.methods)
      return fail(JVMTI_ERROR_OUT_OF_MEMORY, "proxy IfTable allocation failed");
  }
  return JVMTI_ERROR_NONE;
}
// Bounds the local references the reflection walk creates.
jvmtiError PrepareParents(JNIEnv* jni, jclass proxy, const Interface& target, const std::vector<std::string>& added,
                          const std::vector<Interface>& batch, Table& table, std::string* error) {
  if (jni->PushLocalFrame(64) != JNI_OK) {
    jni->ExceptionClear();
    *error = "proxy parent relinking cannot reserve local references";
    return JVMTI_ERROR_OUT_OF_MEMORY;
  }
  const jvmtiError result = CollectParents(jni, proxy, target, added, batch, table, error);
  jni->PopLocalFrame(nullptr);
  return result;
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
  for (const Interface& target : interfaces) {
    std::vector<jclass> implementing;
    for (jclass proxy : proxies) {
      if (jni->IsAssignableFrom(proxy, target.klass)) implementing.push_back(proxy);
      if (jni->ExceptionCheck()) { jni->ExceptionClear(); *error = "proxy assignability failed"; return JVMTI_ERROR_INTERNAL; }
    }
    if (implementing.empty()) continue;
    const auto* definition = target.definition;
    evolution::ClassSchema incoming;
    std::string reason;
    const evolution::DexView view{definition->class_bytes, static_cast<uint32_t>(definition->class_byte_count),
                                  definition->class_bytes, static_cast<uint32_t>(definition->class_byte_count)};
    if (!evolution::ReadClassDeclarations(view, &incoming, &reason)) {
      *error = target.live->descriptor + ": proxy relinking cannot read the new definition: " + reason;
      return JVMTI_ERROR_INVALID_CLASS_FORMAT;
    }
    const std::set<std::string> before(target.live->interfaces.begin(), target.live->interfaces.end());
    std::vector<std::string> added;
    for (const auto& name : incoming.interfaces) {
      if (!before.count(name)) added.push_back(name);
    }
    for (const auto& name : before) {
      // JBR drops the parent from the proxy; that needs IfTable removal and
      // narrowed-reference cleanup for proxies, which this relink lacks.
      if (std::find(incoming.interfaces.begin(), incoming.interfaces.end(), name) == incoming.interfaces.end()) {
        *error = target.live->descriptor + ": a Proxy implementation cannot drop parent interface " + name;
        return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
      }
    }
    jsize count = 0;
    for (const auto& method : incoming.methods)
      if (Virtual(method.access) && method.name != "<init>" && method.name != "<clinit>") ++count;
    for (jclass proxy : implementing) {
      Entry entry;
      entry.proxy = jni->NewGlobalRef(proxy);
      entry.interface = jni->NewGlobalRef(target.klass);
      jlongArray array = jni->NewLongArray(count);
      if (array) {
        entry.methods = jni->NewGlobalRef(array);
        jni->DeleteLocalRef(array);
      }
      const bool complete = entry.proxy && entry.interface && entry.methods;
      state.entries.push_back(entry);
      if (!complete) {
        if (jni->ExceptionCheck()) jni->ExceptionClear();
        *error = "proxy relinking allocation failed while Native";
        return JVMTI_ERROR_OUT_OF_MEMORY;
      }
      Table* table = nullptr;
      for (auto& known : state.tables)
        if (jni->IsSameObject(known.proxy, proxy)) table = &known;
      if (!table) {
        state.tables.emplace_back();
        table = &state.tables.back();
        table->proxy = jni->NewGlobalRef(proxy);
        if (!table->proxy) {
          if (jni->ExceptionCheck()) jni->ExceptionClear();
          *error = "proxy relinking allocation failed while Native";
          return JVMTI_ERROR_OUT_OF_MEMORY;
        }
      }
      if (!added.empty()) {
        const jvmtiError result = PrepareParents(jni, proxy, target, added, interfaces, *table, error);
        if (result != JVMTI_ERROR_NONE) return result;
      }
    }
  }
  return JVMTI_ERROR_NONE;
}

bool Plan(const VerifiedThread& thread, abi::ClassRedefinitionPrefix* self,
          const abi::RedefinitionDataIter& data, int selected, std::string* why) {
  State* state = CurrentState();
  if (!state) return true;
  auto* old_classes = data.Classes(true);
  auto* new_classes = data.Classes(false);
  if (!old_classes || !new_classes) return true;
  if (old_classes->length != new_classes->length) return Fail(why, "proxy class arrays differ");
  for (Entry& entry : state->entries) {
    void* target = class_layout::DecodeClass(services.mirror_class, self, static_cast<jclass>(entry.interface));
    for (int32_t i = 0; i < old_classes->length; ++i) {
      if (class_layout::Element<void>(thread, old_classes, i) != target) continue;
      entry.class_index = i;
      if (!kRelinkers[selected].plan(thread, self, data, *state, entry, why)) return false;
      break;
    }
  }
  // A proxy's added parents commit with the owner that commits their anchor.
  for (Table& table : state->tables) {
    if (table.planned || table.inserted.empty()) continue;
    void* proxy = services.decode_global(state->vm, table.proxy);
    void* anchor = services.decode_global(state->vm, table.anchor);
    for (const Entry& entry : state->entries) {
      if (entry.index != data.index || services.decode_global(state->vm, entry.proxy) != proxy ||
          services.decode_global(state->vm, entry.interface) != anchor) continue;
      if (!kRelinkers[selected].plan_parents(thread, self, *state, table, why)) return false;
      table.owner = data.index;
      table.planned = true;
      break;
    }
  }
  return true;
}

void Apply(abi::ClassRedefinitionPrefix* self, const abi::RedefinitionDataIter& data) noexcept {
  State* state = CurrentState();
  const int selected = class_layout::Selected();
  const auto thread = runtime_thread::Ready();
  if (!state || selected < 0 || !thread) return;
  bool applied = false;
  for (Entry& entry : state->entries) {
    if (entry.index != data.index) continue;
    kRelinkers[selected].apply(*thread, *state, entry,
        class_layout::Element<void>(*thread, data.Classes(false), entry.class_index));
    applied = true;
  }
  if (!applied) return;
  for (auto& table : state->tables)
    if (table.planned && !table.swapped && table.owner == data.index) kRelinkers[selected].swap(*thread, *state, table);
  for (auto& table : state->tables) {
    if (!table.imt || table.published) continue;
    imt_sealing::Publish(services.decode_global(state->vm, table.proxy), selected, table.imt);
    table.published = true;
  }
  (void)self;
}
}  // namespace artdcevm::proxy_relink
