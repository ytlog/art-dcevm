#include "platform/common/proxy_relink.h"

#include <android/log.h>
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
struct Table {
  jobject proxy = nullptr;
  void** imt = nullptr;
  bool built = false, published = false;
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
    for (auto& table : tables) if (table.proxy) jni->DeleteGlobalRef(table.proxy);
  }
};
State* CurrentState() {
  return transaction::PartOf<State>(transaction::Current(), &transaction::Context::proxies);
}

bool Virtual(uint32_t flags) {
  return !(flags & (evolution::kAccStatic | evolution::kAccPrivate | evolution::kAccConstructor));
}

struct Ops {
  bool (*plan)(const VerifiedThread&, abi::ClassRedefinitionPrefix*, const abi::RedefinitionDataIter&,
               State&, Entry&, std::string*);
  void (*apply)(const VerifiedThread&, State&, Entry&, void* new_interface);
};

template <typename Class, typename Method> struct Relinker {
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
    const std::vector<Method*> methods = Virtuals(new_interface);
    if (static_cast<size_t>(array->length) != methods.size())
      return Fail(why, "new interface method count differs from the submitted DEX");
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
    entry.prototypes.clear();
    for (size_t j = 0; j < methods.size(); ++j) {
      evolution::Member identity;
      if (!class_layout::MethodIdentity<Class>(thread, methods[j], &identity, why)) return false;
      Method* target = methods[j];
      for (const auto& [method, prototype] : proxied) {
        if (prototype.name != identity.name || prototype.descriptor != identity.descriptor) continue;
        target = method;
        // Repoint only the proxy methods made from this interface's methods.
        auto* declaring = class_layout::Heap<void>(thread, static_cast<Method*>(method->data)->declaring_class);
        if (declaring == old_interface) entry.prototypes.emplace_back(method, methods[j]);
        break;
      }
      array->elements[j] = reinterpret_cast<uintptr_t>(target);
    }
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
    auto* iftable = class_layout::Heap<abi::ObjectArray>(thread, proxy->iftable);
    const int32_t slot = entry.iftable_slot;
    if (!iftable || 2 * slot + 1 >= iftable->length ||
        class_layout::Heap<void>(thread, iftable->elements[2 * slot]) != new_interface) {
      __android_log_write(ANDROID_LOG_FATAL, "ART-DCEVM", "proxy IfTable does not name the new interface");
      return;
    }
    iftable->elements[2 * slot + 1] = class_layout::Reference(array);
    const size_t card_table_offset = runtime_thread::Layout(thread).exception - sizeof(void*);
    auto* card_table = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(runtime_thread::Current(thread)) +
                                                    card_table_offset);
    card_table[reinterpret_cast<uintptr_t>(iftable) >> kCardShift] = kCardDirty;
    for (const auto& [object, prototype] : entry.prototypes) {
      auto* method = static_cast<Method*>(object);
      method->dex_method_index = static_cast<Method*>(prototype)->dex_method_index;
      method->data = prototype;
    }
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
    const std::set<std::string> after(incoming.interfaces.begin(), incoming.interfaces.end());
    if (before != after) {
      *error = target.live->descriptor + ": a Proxy implementation cannot follow parent interface changes";
      return JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
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
      bool known = false;
      for (const auto& table : state.tables) known |= jni->IsSameObject(table.proxy, proxy);
      if (!known) {
        Table table;
        table.proxy = jni->NewGlobalRef(proxy);
        state.tables.push_back(table);
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
  for (auto& table : state->tables) {
    if (!table.imt || table.published) continue;
    imt_sealing::Publish(services.decode_global(state->vm, table.proxy), selected, table.imt);
    table.published = true;
  }
  (void)self;
}
}  // namespace artdcevm::proxy_relink
