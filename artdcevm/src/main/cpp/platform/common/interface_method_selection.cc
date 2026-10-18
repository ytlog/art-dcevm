#include "platform/common/interface_method_selection.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <unordered_set>
#include "evolution/linking_dex.h"
#include "platform/common/art_method_identity.h"
#include "platform/common/class_layout_table.h"
#include "platform/common/interface_redefinition_internal.h"
#include "platform/common/imt_sealing.h"
#include "platform/symbols/interface_selection_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"

namespace artdcevm::interface_method_selection {
namespace {
namespace abi = abi::arm64;
namespace iface = interface_redefinition;
using namespace iface::detail;
using runtime_thread::VerifiedThread;
using abi::Handle;
using Key = std::pair<std::string, std::string>;
constexpr uint32_t kCopied = 0x01000000;
constexpr uint32_t kDirect = evolution::kAccStatic | evolution::kAccPrivate | evolution::kAccConstructor;

// DexFile has a virtual destructor. This source declaration is used solely to
// destroy the owned result of DexFileLoader::OpenCommon, never to read fields.
struct OwnedDex { virtual ~OwnedDex() = default; };
struct DexContainer { virtual ~DexContainer() = default; };
using OpenDex = std::unique_ptr<OwnedDex> (*)(const uint8_t*, size_t, const uint8_t*, size_t,
    const std::string&, uint32_t, const void*, bool, bool, std::string*, std::unique_ptr<DexContainer>, void*);
struct Services {
  JavaVM* vm = nullptr;
  OpenDex open = nullptr;
  void* (*methods)(void*, void*, void*, size_t) = nullptr;
  void* (*allocator)(void*, void*) = nullptr;
  void* (*pointers)(void*, void*, size_t) = nullptr;
  void (*update)(void*, void*, void*) = nullptr;
  jobject (*add_global)(JavaVM*, void*, void*) = nullptr;
  void (*delete_global)(JavaVM*, void*, jobject) = nullptr;
  void (*throw_new)(void*, const char*, const char*) = nullptr;
  void* (*current)() = nullptr;
  void* bridge = nullptr;
  jclass probe_class = nullptr;
  jmethodID probe = nullptr;
};
Services services;
std::atomic<bool> ready{false};
std::atomic<int> active_release{0};
std::atomic<void*> runtime{nullptr};
std::mutex registry_mutex;
std::unordered_set<void*> access_errors;
std::unordered_set<void*> invocation_errors;
void* original_abstract = nullptr;
void* original_abstract_receiver = nullptr;
void* original_interpreter_bridge = nullptr;
void* original_invocation = nullptr;
void* original_invocation_receiver = nullptr;
thread_local bool helper = false;
thread_local void* current_linker = nullptr;
thread_local Handle helper_pending{};

struct Registration { std::vector<void*> methods, access; jobject owner = nullptr; bool committed = false; };
void Remove(const Registration& registration) noexcept {
  std::lock_guard<std::mutex> lock(registry_mutex);
  for (void* method : registration.methods) { access_errors.erase(method); invocation_errors.erase(method); }
  if (registration.owner) services.delete_global(services.vm, services.current(), registration.owner);
}
struct State final : transaction::Part {
  std::vector<Registration> staged;
  ~State() override { for (const auto& registration : staged) if (!registration.committed) Remove(registration); }
};
State& SelectionState() {
  auto& part = transaction::Current()->selection;
  if (!part) part = std::make_unique<State>();
  return *static_cast<State*>(part.get());
}
const char* InvocationError(void* method) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  if (!invocation_errors.count(method)) return nullptr;
  if (access_errors.count(method)) return "Ljava/lang/IllegalAccessError;";
  return (static_cast<abi::ArtMethodPrefix*>(method)->access_flags.load() & 0x00400000)
      ? "Ljava/lang/IncompatibleClassChangeError;" : "Ljava/lang/AbstractMethodError;";
}
void AbstractError(void* method) noexcept {
  if (const char* exception = InvocationError(method))
    services.throw_new(services.current(), exception, "interface method has no accessible implementation");
  else reinterpret_cast<void (*)(void*)>(original_abstract)(method);
}
void AbstractReceiverError(void* method, void* receiver) noexcept {
  if (const char* exception = InvocationError(method))
    services.throw_new(services.current(), exception, "interface method has no accessible implementation");
  else reinterpret_cast<void (*)(void*, void*)>(original_abstract_receiver)(method, receiver);
}
void InvocationFailure(void* method) noexcept {
  if (const char* exception = InvocationError(method))
    services.throw_new(services.current(), exception, "interface method has no accessible implementation");
  else reinterpret_cast<void (*)(void*)>(original_invocation)(method);
}
void InvocationReceiverFailure(void* method, void* receiver) noexcept {
  if (const char* exception = InvocationError(method))
    services.throw_new(services.current(), exception, "interface method has no accessible implementation");
  else reinterpret_cast<void (*)(void*, void*)>(original_invocation_receiver)(method, receiver);
}
// interpreter/shadow_frame.h, Android 11--17. Only the two native
// leading members are read, with no heap reference or frame offset inference.
struct ShadowFramePrefix { void* link; abi::ArtMethodPrefix* method; };
static_assert(offsetof(ShadowFramePrefix, method) == sizeof(void*));
void InterpreterBridge(void* thread, const void* accessor, ShadowFramePrefix* frame, uint64_t* result) noexcept {
  if (frame && (frame->method->access_flags.load(std::memory_order_relaxed) & evolution::kAccAbstract)) {
    if (result) *result = 0;
    // Older MethodHandle dispatch enters this bridge without checking abstract
    // methods, including ART's ordinary mirandas in classes with no hidden
    // declarations. Never interpret their absent CodeItem. Keep registered IAE
    // targets first, and let ART choose the normal invocation-time exception
    // for all other abstract targets.
    if (const char* exception = InvocationError(frame->method))
      services.throw_new(thread, exception, "interface method has no accessible implementation");
    else if (original_invocation)
      reinterpret_cast<void (*)(void*)>(original_invocation)(frame->method);
    else
      reinterpret_cast<void (*)(void*, void*)>(original_invocation_receiver)(frame->method, nullptr);
    return;
  }
  using Function = void (*)(void*, const void*, ShadowFramePrefix*, uint64_t*);
  reinterpret_cast<Function>(original_interpreter_bridge)(thread, accessor, frame, result);
}
void Probe(JNIEnv* jni, jclass) noexcept {
  try { runtime_thread::Probe(jni); }
  catch (...) {
    // Keep C++ allocation failures inside the native boundary. FindClass or
    // ThrowNew supplies a pending Java OOM even when the local table is full.
    jclass error = jni->FindClass("java/lang/OutOfMemoryError");
    if (error) { jni->ThrowNew(error, "native interface thread probe failed"); jni->DeleteLocalRef(error); }
  }
}

void Mark(const VerifiedThread& thread, void* object) {
  // gc/accounting/card_table.h and ThreadTlsPtrPrefix (Android 11--17), as in
  // proxy_relink: ART's normal card barrier after a private reference store.
  const size_t offset = runtime_thread::Layout(thread).exception - sizeof(void*);
  auto* cards = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(runtime_thread::Current(thread)) + offset);
  cards[reinterpret_cast<uintptr_t>(object) >> 10] = 0x70;
}
struct Dex {
  std::vector<uint8_t> bytes;
  std::unique_ptr<OwnedDex> owned;
  const abi::DexFilePrefix* Get() const { return reinterpret_cast<const abi::DexFilePrefix*>(owned.get()); }
  Dex(std::vector<uint8_t> input, const evolution::DexView* source = nullptr) : bytes(std::move(input)) {
    const bool separate = source && source->data != source->begin;
    const uint8_t* data = separate ? source->data : bytes.data();
    const size_t size = separate ? source->data_size : bytes.size();
    std::string error;
    owned = services.open(bytes.data(), bytes.size(), data, size, "art-dcevm-interface-selection", 0,
                          nullptr, !source, !source, &error, nullptr, nullptr);
    if (!owned) throw std::runtime_error("cannot open private interface selection DEX: " + error);
  }
};
class CacheMask {
 public:
  CacheMask(Handle cache, const abi::DexFilePrefix* dex) : cache_(cache), original_(cache.Get<abi::DexCache>()->dex_file) {
    cache_.Get<abi::DexCache>()->dex_file = reinterpret_cast<uintptr_t>(dex);
  }
  ~CacheMask() { cache_.Get<abi::DexCache>()->dex_file = original_; }
 private:
  Handle cache_;
  uint64_t original_;
};
class ClassCacheMask {
 public:
  ClassCacheMask(Handle klass, Handle original, Handle replacement) : klass_(klass), original_(original) {
    process->set_cache(klass_.Get<void>(), replacement.Get<void>());
  }
  ~ClassCacheMask() { process->set_cache(klass_.Get<void>(), original_.Get<void>()); }
 private:
  Handle klass_, original_;
};
class HelperScope {
 public:
  HelperScope(Handle cache, const std::vector<Handle>& types, Handle pending)
      : state_(CurrentState()), old_cache_(state_->selection_cache), old_types_(state_->selection_types),
        old_helper_(helper), old_pending_(helper_pending) {
    state_->selection_cache = cache;
    state_->selection_types = &types;
    helper = true;
    helper_pending = pending;
  }
  ~HelperScope() {
    DiscardClass(helper_pending.Get<void>());
    helper_pending = old_pending_;
    helper = old_helper_;
    state_->selection_cache = old_cache_;
    state_->selection_types = old_types_;
  }
 private:
  iface::detail::State* state_;
  Handle old_cache_;
  const std::vector<Handle>* old_types_;
  bool old_helper_;
  Handle old_pending_;
};

struct Ops {
  bool (*link)(LinkMethodsFunction, void*, void*, Handle, Handle, bool*, void**);
};
template <typename Class, typename Method> struct Selector {
  static Key Identity(Method* method) {
    evolution::Member member;
    std::string why;
    if (!class_layout::MethodIdentity<Class>(Thread(), method, &member, &why)) throw std::runtime_error(why);
    return {std::move(member.name), std::move(member.descriptor)};
  }
  static Class* Heap(uint32_t reference) { return class_layout::Heap<Class>(Thread(), reference); }
  static std::vector<Method*> Virtuals(Class* klass) {
    std::vector<Method*> result;
    auto* methods = class_layout::Members<Method>(klass->methods);
    for (uint32_t i = 0; methods && i < methods->length; ++i) {
      const uint32_t flags = methods->At(i)->access_flags.load(std::memory_order_relaxed);
      if (!(flags & (kDirect | kCopied))) result.push_back(methods->At(i));
    }
    return result;
  }
  static bool Link(LinkMethodsFunction original, void* boundary, void* thread, Handle klass,
                   Handle interfaces, bool* conflict, void** imt) {
    void* linker = current_linker;
    auto* receiver = klass.Get<Class>();
    if (receiver->access_flags & (evolution::kAccInterface | 0x00040000u) || !receiver->dex_cache || !receiver->super_class)
      return original(boundary, thread, klass, interfaces, conflict, imt);
    // Proxy classes have no ordinary ClassDef and already use proxy_relink's
    // final interface graph and generated InvocationHandler methods.
    if (receiver->dex_class_def_index < 0 || !class_layout::DexOf(Thread(), receiver->dex_cache))
      return original(boundary, thread, klass, interfaces, conflict, imt);
    const auto dex = DexViewOf(*class_layout::DexOf(Thread(), receiver->dex_cache));
    std::vector<uint16_t> indices;
    std::string why;
    if (!evolution::ReadClassInterfaceIndices(dex, receiver->dex_class_def_index, &indices, &why))
      throw std::runtime_error(why);
    std::vector<void*> graph;
    auto append = [&](void* type) {
      if (type && std::find(graph.begin(), graph.end(), type) == graph.end()) graph.push_back(type);
    };
    auto* super = Heap(receiver->super_class);
    auto* inherited = class_layout::Heap<abi::ObjectArray>(Thread(), super->iftable);
    for (int32_t i = 0; inherited && i < inherited->length; i += 2)
      append(runtime_thread::Read(Thread(), inherited->elements[i]));
    for (uint16_t index : indices) {
      std::string descriptor;
      if (!evolution::ReadTypeDescriptor(dex, index, &descriptor, &why)) throw std::runtime_error(why);
      void* defining_loader = runtime_thread::Read(Thread(), klass.Get<Class>()->class_loader);
      // Descriptor lookup is read-only. DoLookupResolvedType would cache the
      // old interface before redirection and defeat modern inline lookups.
      void* parent = process->lookup_view ? process->lookup_view(linker, thread, descriptor, defining_loader)
          : process->lookup_chars(linker, thread, descriptor.c_str(), defining_loader);
      // Respect the current batch's replacement lookup, including another group.
      if (const auto* state = CurrentState(); state->linking) {
        const auto& linking = *state->linking;
        for (size_t i = 0; i < linking.old_types->size(); ++i)
          if ((*linking.old_types)[i].Get<void>() == parent)
            parent = runtime_thread::Read(Thread(), linking.new_types.Get<abi::ObjectArray>()->elements[i]);
      }
      bool pending = false;
      if (void* replacement = ReplacementFor(parent, &pending)) parent = replacement;
      if (!parent || pending) throw std::runtime_error("interface selection parent is not linked");
      auto* table = class_layout::Heap<abi::ObjectArray>(Thread(), static_cast<Class*>(parent)->iftable);
      for (int32_t i = 0; table && i < table->length; i += 2) append(runtime_thread::Read(Thread(), table->elements[i]));
      append(parent);
    }
    if (graph.empty()) return original(boundary, thread, klass, interfaces, conflict, imt);
    std::map<Key, Method*> class_methods;
    std::set<Key> hidden, private_declarations;
    const auto collect_class_methods = [&] {
      Class* receiver = klass.Get<Class>();
      class_methods.clear(); hidden.clear(); private_declarations.clear();
      for (Class* type = receiver; type; type = Heap(type->super_class)) {
        auto* methods = class_layout::Members<Method>(type->methods);
        for (uint32_t i = 0; methods && i < methods->length; ++i) {
          Method* method = methods->At(i);
          const uint32_t flags = method->access_flags.load(std::memory_order_relaxed);
          if (flags & (kCopied | evolution::kAccStatic | evolution::kAccConstructor)) continue;
          Key key = Identity(method);
          if (!(flags & evolution::kAccPublic)) hidden.insert(key);
          if (flags & evolution::kAccPrivate) private_declarations.insert(key);
          if (!(flags & evolution::kAccPrivate)) class_methods.emplace(std::move(key), method);
        }
      }
    };
    collect_class_methods();
    bool affected = false;
    for (void* interface : graph)
      for (Method* method : Virtuals(static_cast<Class*>(interface)))
        affected |= hidden.count(Identity(method)) != 0;
    if (!affected) return original(boundary, thread, klass, interfaces, conflict, imt);
ARTDCEVM_TEST(
    __android_log_write(ANDROID_LOG_INFO, "ART-DCEVM", ("selecting interfaces for " +
        class_layout::ClassDescriptor(Thread(), *klass.Get<Class>())).c_str());
)
    if (!imt_sealing::ProtectInvocations()) throw std::runtime_error("interface selection requires protected IMT linking");

    Roots roots(thread, graph.size() + 8);
    std::vector<std::pair<std::string, Handle>> parents;
    std::vector<std::string> names;
    for (void* parent : graph) {
      std::string name = class_layout::ClassDescriptor(Thread(), *static_cast<Class*>(parent));
      names.push_back(name);
      parents.emplace_back(std::move(name), roots.Add(parent));
    }
    Handle cache = roots.Add(runtime_thread::Read(Thread(), klass.Get<Class>()->dex_cache));
    Handle object = roots.Add(klass.Get<void>());
    while (object.Get<Class>()->super_class) Assign(object, runtime_thread::Read(Thread(), object.Get<Class>()->super_class));
    Handle anchor = parents.front().second;
    Handle loader = roots.Add(runtime_thread::Read(Thread(), anchor.Get<Class>()->class_loader));
    Handle helper_cache = roots.Add(nullptr), helper_class = roots.Add(nullptr), pending = roots.Add(nullptr);
    Handle vtable_root = roots.Add(nullptr), linking_cache = roots.Add(nullptr);
    static std::atomic<uint64_t> sequence{0};
    Dex resolver(evolution::linking_dex::Class("Lartdcevm/link/Selection" + std::to_string(++sequence) + ";", names));
    std::string error;
    abi::AllocationDriver driver{{nullptr, JVMTI_ERROR_INTERNAL, runtime.load(), thread}, 1, {}, &error};
    abi::AllocationDefinition definition{{&driver.prefix, nullptr, resolver.Get()}, "interface selection helper"};
    Assign(helper_cache, process->allocate_cache(&definition.prefix, loader));
    if (!helper_cache.Get<void>()) throw std::bad_alloc{};
    // The helper has no class methods. ART alone supplies defaults, mirandas
    // and conflict methods for the complete final graph, all owned by real
    // interface declaring classes. No helper descriptor is published.
    const auto resolver_view = DexViewOf(*resolver.Get());
    std::vector<Handle> resolved_types(evolution::linking_dex::Read32(resolver.bytes.data() + 64));
    for (size_t i = 0; i < resolved_types.size(); ++i) {
      std::string name;
      if (!evolution::ReadTypeDescriptor(resolver_view, i, &name, &why)) throw std::runtime_error(why);
      if (name == "Ljava/lang/Object;") resolved_types[i] = object;
      for (const auto& [descriptor, root] : parents) if (name == descriptor) resolved_types[i] = root;
    }
    HelperScope resolving(helper_cache, resolved_types, pending);
    Assign(helper_class, process->allocate_class(&definition.prefix, anchor, object, helper_cache, 0));
    if (!helper_class.Get<void>()) throw std::runtime_error("ART could not link the interface selection helper");
    Assign(pending, helper_class.Get<void>());
    Dex skeleton(evolution::linking_dex::WithoutInterfaces(dex, klass.Get<Class>()->dex_class_def_index), &dex);
    // A normal class definition can share its original DexCache with already
    // published classes. Give only this unpublished class a fresh cache before
    // masking the DEX; never change a cache another class can observe.
    Assign(loader, runtime_thread::Read(Thread(), klass.Get<Class>()->class_loader));
    abi::AllocationDefinition source{{&driver.prefix, nullptr, class_layout::DexOf(Thread(), klass.Get<Class>()->dex_cache)},
                                     "interface class-method linking"};
    Assign(linking_cache, process->allocate_cache(&source.prefix, loader));
    if (!linking_cache.Get<void>()) throw std::bad_alloc{};
    {
      ClassCacheMask class_cache(klass, cache, linking_cache);
      CacheMask mask(linking_cache, skeleton.Get());
      if (!original(boundary, thread, klass, interfaces, conflict, imt)) return false;
    }
    // Original linking may have reallocated the private method array. Refresh
    // native targets before copying or remapping any of its pointers.
    collect_class_methods();
    auto* baseline = class_layout::Heap<abi::PointerArray>(Thread(), klass.Get<Class>()->vtable);
    std::vector<Method*> vtable;
    for (int32_t i = 0; i < baseline->length; ++i)
      vtable.push_back(reinterpret_cast<Method*>(baseline->elements[i]));
    auto* table = class_layout::Heap<abi::ObjectArray>(Thread(), helper_class.Get<Class>()->iftable);
    std::map<Key, Method*> selected;
    std::map<Key, bool> inaccessible, missing;
    const auto inherited_interface = [&](Method* method) {
      void* owner = runtime_thread::Read(Thread(), method->declaring_class);
      auto* superclass = Heap(klass.Get<Class>()->super_class);
      auto* inherited = class_layout::Heap<abi::ObjectArray>(Thread(), superclass->iftable);
      for (int32_t i = 0; inherited && i < inherited->length; i += 2)
        if (runtime_thread::Read(Thread(), inherited->elements[i]) == owner) return true;
      return false;
    };
    for (int32_t i = 0; i < table->length; i += 2) {
      auto* iface = class_layout::Heap<Class>(Thread(), table->elements[i]);
      auto declarations = Virtuals(iface);
      auto* array = class_layout::Heap<abi::PointerArray>(Thread(), table->elements[i + 1]);
      if (!array) continue;
      if (static_cast<size_t>(array->length) != declarations.size()) throw std::runtime_error("selection IfTable count mismatch");
      for (size_t j = 0; j < declarations.size(); ++j) {
        Key key = Identity(declarations[j]);
        if (selected.count(key)) continue;
        auto actual = class_methods.find(key);
        const bool denied = actual != class_methods.end() && !(actual->second->access_flags.load() & evolution::kAccPublic);
        selected[key] = actual != class_methods.end() && !denied ? actual->second
            : reinterpret_cast<Method*>(array->elements[j]);
        inaccessible[key] = denied;
        // JBR's defaultMethods::find_empty_vtable_slots reconsiders inherited
        // defaults only when ordinary class lookup finds no declaration. A
        // private declaration can suppress that inherited slot, even on the
        // receiver itself. It does not suppress a newly implemented interface's
        // default: a class first implementing I below a private-method base can
        // call I's default, while its child selects AME. Compare interface class
        // identities in the final superclass graph, never names or packages.
        missing[key] = actual == class_methods.end() && private_declarations.count(key) &&
            inherited_interface(selected[key]) &&
            !(selected[key]->access_flags.load() & evolution::kAccAbstract);
      }
    }
    auto* old_methods = class_layout::Members<Method>(klass.Get<Class>()->methods);
    const size_t original_count = old_methods ? old_methods->length : 0;
    size_t copies = 0;
    for (const auto& [key, method] : selected)
      if (inaccessible[key] || class_methods.find(key) == class_methods.end()) ++copies;
    if (original_count + copies > UINT16_MAX || vtable.size() + copies > UINT16_MAX)
      throw std::runtime_error("too many interface selection methods");
    void* allocator = services.allocator(linker, runtime_thread::Read(Thread(), klass.Get<Class>()->class_loader));
    auto* methods = static_cast<abi::LengthPrefixedArray<Method>*>(services.methods(linker, thread, allocator, original_count + copies));
    if (!methods) throw std::bad_alloc{};
    for (size_t i = 0; i < original_count; ++i) {
      std::memcpy(methods->At(i), old_methods->At(i), sizeof(Method));
      methods->At(i)->declaring_class = class_layout::Reference(runtime_thread::Read(Thread(), methods->At(i)->declaring_class));
    }
    auto remap = [&](Method* method) {
      return old_methods && old_methods->Contains(method) ? methods->At(method - old_methods->At(0)) : method;
    };
    for (auto& method : vtable) method = remap(method);
    Registration registration;
    size_t next = original_count;
    for (auto& [key, method] : selected) {
      if (!inaccessible[key] && class_methods.find(key) != class_methods.end()) { method = remap(method); continue; }
      Method* copy = methods->At(next++);
      std::memcpy(copy, method, sizeof(Method));
      copy->declaring_class = class_layout::Reference(runtime_thread::Read(Thread(), copy->declaring_class));
      uint32_t flags = copy->access_flags.load() | kCopied;
      if (inaccessible[key] || missing[key]) {
        flags = (flags & ~(0x80000000u | 0x08000000u | 0x00400000u | 0x00080000u | evolution::kAccNative)) |
            evolution::kAccPublic | evolution::kAccAbstract;
        copy->data = nullptr;
        ClearCode(copy);
        copy->quick_entry = services.bridge;
        if (inaccessible[key]) registration.access.push_back(copy);
      }
      copy->access_flags.store(flags);
      if (flags & evolution::kAccAbstract) registration.methods.push_back(copy);
      // Reuse inherited copied slots, preserving ordinary class-method slots.
      size_t slot = vtable.size();
      for (size_t i = 0; i < vtable.size(); ++i)
        if ((vtable[i]->access_flags.load() & kCopied) && Identity(vtable[i]) == key) { slot = i; break; }
      copy->method_index = slot;
      if (slot == vtable.size()) vtable.push_back(copy); else vtable[slot] = copy;
      method = copy;
    }
    if (services.update) services.update(linker, klass.Get<void>(), methods);
    else {
      // UpdateClassMethods' fixed body keeps the declared-method boundaries
      // unchanged and replaces only methods_, followed by the card barrier.
      klass.Get<Class>()->methods = reinterpret_cast<uintptr_t>(methods);
    }
    Mark(Thread(), klass.Get<void>());
    Assign(vtable_root, services.pointers(linker, thread, vtable.size()));
    if (!vtable_root.Get<void>()) throw std::bad_alloc{};
    for (size_t i = 0; i < vtable.size(); ++i) vtable_root.Get<abi::PointerArray>()->elements[i] = reinterpret_cast<uintptr_t>(vtable[i]);
    // Re-read moved handles after the managed vtable allocation.
    table = class_layout::Heap<abi::ObjectArray>(Thread(), helper_class.Get<Class>()->iftable);
    for (int32_t i = 0; i < table->length; i += 2) {
      auto declarations = Virtuals(class_layout::Heap<Class>(Thread(), table->elements[i]));
      auto* array = class_layout::Heap<abi::PointerArray>(Thread(), table->elements[i + 1]);
      for (size_t j = 0; array && j < declarations.size(); ++j)
        array->elements[j] = reinterpret_cast<uintptr_t>(selected.at(Identity(declarations[j])));
    }
    receiver = klass.Get<Class>();
    receiver->iftable = class_layout::Reference(table);
    receiver->vtable = class_layout::Reference(vtable_root.Get<void>());
    Mark(Thread(), receiver);
    if (!registration.methods.empty()) {
      void* owner = runtime_thread::Read(Thread(), receiver->class_loader);
      registration.owner = services.add_global(services.vm, thread, owner ? owner : receiver);
      if (!registration.owner) throw std::bad_alloc{};
      // Stage ownership before changing the registry: C++ allocation failures
      // cannot leave unowned native keys visible to invocation threads.
      auto& staged = SelectionState().staged;
      try { staged.push_back(std::move(registration)); }
      catch (...) { services.delete_global(services.vm, thread, registration.owner); throw; }
      auto& entry = staged.back();
      std::lock_guard<std::mutex> lock(registry_mutex);
      try {
        for (void* method : entry.methods) invocation_errors.insert(method);
        for (void* method : entry.access) access_errors.insert(method);
      } catch (...) {
        for (void* method : entry.methods) { invocation_errors.erase(method); access_errors.erase(method); }
        throw;
      }
    }
    active_release.store(CurrentState()->selected_release);
    return true;
  }
  static constexpr Ops Make(int) { return {Link}; }
};
const auto kOps = class_layout::Table<Selector, Ops>();
}  // namespace

bool Available() { return ready.load() && imt_sealing::Available(); }
bool LinkingHelper() { return helper; }
void Complete(bool success) noexcept {
  auto* context = transaction::Current();
  auto* state = context ? static_cast<State*>(context->selection.get()) : nullptr;
  if (!state || !success) return;
  // The JNI global table owns each successful root. Deliberately retain it
  // through process lifetime, as for retired members; commit allocates nothing.
  for (auto& entry : state->staged) entry.committed = true;
}

bool LinkMethods(LinkMethodsFunction original, void* linker, void* thread, abi::Handle klass,
                 abi::Handle interfaces, bool* conflict, void** imt) noexcept {
  const auto* state = CurrentState();
  if (!Available() || helper || !state || !state->runtime_verified || !state->thread)
    return original(linker, thread, klass, interfaces, conflict, imt);
  try {
    const int index = state->selected_release - 11;
    return index >= 0 && index < static_cast<int>(kOps.size())
        ? kOps[index].link(original, linker, thread, klass, interfaces, conflict, imt)
        : original(linker, thread, klass, interfaces, conflict, imt);
  } catch (const std::bad_alloc&) {
    services.throw_new(thread, "Ljava/lang/OutOfMemoryError;", "interface selection allocation failed");
  } catch (const std::exception& error) {
    services.throw_new(thread, "Ljava/lang/LinkageError;", error.what());
  } catch (...) {
    services.throw_new(thread, "Ljava/lang/LinkageError;", "interface selection failed");
  }
  return false;
}

bool LinkClass(LinkClassFunction original, void* linker, void* thread, const char* descriptor,
               abi::Handle klass, abi::Handle interfaces, abi::Handle* output) noexcept {
  struct LinkerScope {
    void* previous;
    explicit LinkerScope(void* value) : previous(std::exchange(current_linker, value)) {}
    ~LinkerScope() { current_linker = previous; }
  } linker_scope(linker);
  if (helper) {
    if (helper_pending.reference) Assign(helper_pending, klass.Get<void>());
    const bool result = original(linker, thread, descriptor, klass, interfaces, output);
    if (output && output->Get<void>() && helper_pending.reference) Assign(helper_pending, output->Get<void>());
    return result;
  }
  if (!Available()) return original(linker, thread, descriptor, klass, interfaces, output);
  try {
    const auto* state = CurrentState();
    if (state && state->runtime_verified && state->thread) {
      if (state->attempt) runtime.store(state->attempt->driver->runtime);
      auto& staged = SelectionState().staged;
      const size_t first = staged.size();
      const bool result = original(linker, thread, descriptor, klass, interfaces, output);
      // Normal definitions remain published even if an enclosing redefine
      // subsequently aborts. Private replacements wait for the batch result.
      if (descriptor && result)
        for (size_t i = first; i < staged.size(); ++i) staged[i].committed = true;
      return result;
    }
    const int release = active_release.load();
    if (!release || !runtime.load()) return original(linker, thread, descriptor, klass, interfaces, output);
    transaction::Scope scope;
    JNIEnv* jni = nullptr;
    if (!runtime_thread::Restore(thread)) {
      if (services.vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6) != JNI_OK || !jni)
        return original(linker, thread, descriptor, klass, interfaces, output);
      // First link on an unprobed attachment: the preinitialized native method
      // supplies the Native proof. Subsequent links reuse its thread-local facts.
      jni->CallStaticVoidMethod(services.probe_class, services.probe);
      if (jni->ExceptionCheck()) return false;
    }
    auto proof = runtime_thread::Ready();
    if (!proof || runtime_thread::Current(*proof) != thread) return false;
    jni = runtime_thread::Environment(*proof);
    auto next = std::make_unique<iface::detail::State>(jni, nullptr, 0, nullptr);
    next->runtime = abi::RuntimeFor(release);
    next->selected_release = release;
    next->thread = proof;
    next->thread_layout = runtime_thread::LayoutFor(*proof, release);
    next->runtime_verified = next->runtime && next->thread_layout;
    scope.context().interfaces = std::move(next);
    const bool result = original(linker, thread, descriptor, klass, interfaces, output);
    Complete(result);
    return result;
  } catch (...) {
    services.throw_new(thread, "Ljava/lang/LinkageError;", "interface linking context failed");
    return false;
  }
}

bool Prepare(JavaVM* vm, JNIEnv* jni, std::vector<Hook>* hooks, std::string* why) {
  using namespace symbols::interface_selection;
  ready.store(false);
  services.vm = vm;
  services.open = reinterpret_cast<OpenDex>(symbols::Find(kOpenDex));
  services.methods = reinterpret_cast<decltype(services.methods)>(symbols::Find(kAllocMethods));
  services.allocator = reinterpret_cast<decltype(services.allocator)>(symbols::Find(kGetAllocator));
  services.pointers = reinterpret_cast<decltype(services.pointers)>(symbols::Find(kAllocPointers));
  services.update = reinterpret_cast<decltype(services.update)>(symbols::Find(kUpdateMethods));
  services.add_global = reinterpret_cast<decltype(services.add_global)>(symbols::Find(kAddGlobal));
  services.delete_global = reinterpret_cast<decltype(services.delete_global)>(symbols::Find(kDeleteGlobal));
  services.throw_new = reinterpret_cast<decltype(services.throw_new)>(symbols::Find(symbols::structural::kThrowNewException));
  services.current = reinterpret_cast<decltype(services.current)>(symbols::Find(symbols::runtime_thread::kCurrentThread));
  services.bridge = symbols::Find(kInterpreterBridge);
  void* abstract = symbols::Find(kAbstractError);
  void* receiver = symbols::Find(kAbstractReceiverError);
  void* interpreter = symbols::Find(kInterpreterCall);
  void* invocation = symbols::Find(kInvocationError);
  void* invocation_receiver = symbols::Find(kInvocationReceiverError);
  const std::pair<const char*, bool> required[] = {
      {"OpenCommon", services.open != nullptr}, {"AllocArtMethodArray", services.methods != nullptr},
      {"GetAllocatorForClassLoader", services.allocator != nullptr}, {"AllocPointerArray", services.pointers != nullptr},
      {"AddGlobalRef", services.add_global != nullptr},
      {"DeleteGlobalRef", services.delete_global != nullptr}, {"ThrowNewException", services.throw_new != nullptr},
      {"CurrentFromGdb", services.current != nullptr}, {"interpreter bridge", services.bridge != nullptr},
      {"ThrowAbstractMethodError", abstract || receiver}, {"interpreter invocation bridge", interpreter != nullptr}, {"invocation-time error", invocation || invocation_receiver}};
  for (const auto& [name, found] : required)
    if (!found) return class_layout::Fail(why, std::string("interface selection missing ") + name);
  jclass local = jni->FindClass("com/github/ytlog/artdcevm/internal/InterfaceLinkProbe");
  bool bound = false;
  if (local) {
    services.probe = jni->GetStaticMethodID(local, "probe", "()V");
    if (services.probe && !jni->ExceptionCheck()) {
      services.probe_class = static_cast<jclass>(jni->NewGlobalRef(local));
      if (services.probe_class && !jni->ExceptionCheck()) {
        JNINativeMethod method{const_cast<char*>("probe"), const_cast<char*>("()V"), reinterpret_cast<void*>(Probe)};
        bound = jni->RegisterNatives(local, &method, 1) == JNI_OK;
      }
    }
    jni->DeleteLocalRef(local);
  }
  if (!bound || jni->ExceptionCheck()) {
    jni->ExceptionClear();
    if (services.probe_class) jni->DeleteGlobalRef(services.probe_class);
    services.probe_class = nullptr;
    services.probe = nullptr;
    return class_layout::Fail(why, "interface selection native probe unavailable");
  }
  if (invocation) hooks->push_back({invocation, reinterpret_cast<void*>(InvocationFailure), &original_invocation, kInvocationError.name});
  if (invocation_receiver) hooks->push_back({invocation_receiver, reinterpret_cast<void*>(InvocationReceiverFailure), &original_invocation_receiver, kInvocationReceiverError.name});
  if (abstract) hooks->push_back({abstract, reinterpret_cast<void*>(AbstractError), &original_abstract, kAbstractError.name});
  if (receiver) hooks->push_back({receiver, reinterpret_cast<void*>(AbstractReceiverError), &original_abstract_receiver, kAbstractReceiverError.name});
  hooks->push_back({interpreter, reinterpret_cast<void*>(InterpreterBridge), &original_interpreter_bridge, kInterpreterCall.name});
  ready.store(true);
  return true;
}
}  // namespace artdcevm::interface_method_selection
