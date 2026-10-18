#include "platform/common/invoke_kind.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "platform/common/runtime_roots.h"
#include "platform/symbols/interface_redefinition_symbols.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/symbols/invoke_kind_symbols.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/runtime_thread_symbols.h"

namespace artdcevm::invoke_kind {
namespace {
using abi::arm64::Handle;
using Caller = void* (*)(void*, void*, uint32_t, void*, int);
using Cache = void* (*)(void*, uint32_t, Handle, Handle, void*, int);
using Nterp = uintptr_t (*)(void*, void*, const uint16_t*);
Caller original_caller = nullptr;
Cache original_cache = nullptr;
using MethodId = void* (*)(void*, uint32_t, void*);
MethodId resolve_method_id = nullptr;
void* (*current_thread)() = nullptr;
void (*throw_exception)(void*, const char*, const char*) = nullptr;
Nterp original_nterp = nullptr;
std::atomic<void*> class_linker{nullptr};
std::atomic<bool> installed{false}, enabled{false};
// ObjPtr<Object> is one uintptr_t in the fixed release ART declarations.
using Select = void* (*)(void*, void*, uintptr_t*, const uint16_t*, bool, bool*);
Select original_virtual = nullptr, original_interface = nullptr;
const char* (*method_descriptor)(void*) = nullptr;
uintptr_t (*method_loader)(void*) = nullptr;
abi::arm64::HandleScopeVisitors scope_visitors;
std::mutex scope_mutex;
std::atomic<bool> scope_verified{false};
const abi::arm64::RuntimeThread* scope_thread_layout = nullptr;
const abi::arm64::InterfaceRuntime* scope_layout = nullptr;
using ObsoleteCache = uintptr_t (*)(void*);
ObsoleteCache original_cache_read = nullptr, original_cache_raw = nullptr;
void* (*mark)(void*) = nullptr;
const bool* use_read_barrier = nullptr;
std::atomic<bool> class_verified{false};
size_t class_access = 0, class_dex_cache = 0, class_ext_data = 0, ext_obsolete_methods = 0;

template <bool read_barrier>
uintptr_t Reference(uint32_t reference) {
  if constexpr (read_barrier)
    if (reference && *use_read_barrier)
      return reinterpret_cast<uintptr_t>(mark(reinterpret_cast<void*>(static_cast<uintptr_t>(reference))));
  return reference;
}

template <bool read_barrier>
uintptr_t CopiedObsoleteCache(void* method) noexcept {
  const auto* prefix = static_cast<abi::arm64::ArtMethodPrefix*>(method);
  constexpr uint32_t kCopiedObsolete = 0x01000000 | abi::arm64::kAccObsoleteMethod;
  if (class_verified.load(std::memory_order_acquire) &&
      (prefix->access_flags.load(std::memory_order_relaxed) & kCopiedObsolete) == kCopiedObsolete) {
    // Class::Alloc creates nonmoving classes. An old frame roots its retained
    // declaring interface; ART intentionally does not replace this reference.
    void* declaring = reinterpret_cast<void*>(static_cast<uintptr_t>(prefix->declaring_class));
    if (runtime_roots::Field<uint32_t>(declaring, class_access) & 0x00200000) { // kAccObsoleteObject.
      // A frame from an earlier non-structural redefinition can still be in
      // ART's normal obsolete map. Preserve that historical DEX selection.
      void* ext = reinterpret_cast<void*>(Reference<read_barrier>(runtime_roots::Field<uint32_t>(declaring, class_ext_data)));
      const auto* methods = ext ? reinterpret_cast<const abi::arm64::PointerArray*>(
          Reference<read_barrier>(runtime_roots::Field<uint32_t>(ext, ext_obsolete_methods))) : nullptr;
      for (int32_t i = 0; methods && i < methods->length; ++i)
        if (methods->elements[i] == reinterpret_cast<uintptr_t>(method))
          return (read_barrier ? original_cache_read : original_cache_raw)(method);
      // The original structural copy lives outside the interface's method
      // array, but its code still belongs to this retained interface's DEX.
      // Match Class::GetDexCache's CC/CMC and no-barrier behavior.
      return Reference<read_barrier>(runtime_roots::Field<uint32_t>(declaring, class_dex_cache));
    }
  }
  return (read_barrier ? original_cache_read : original_cache_raw)(method);
}

bool OwnClass(void* caller, void* resolved) {
  // Class identity includes the defining loader. Obsolete ART class objects
  // deliberately survive reference replacement, so pointer equality is insufficient.
  return method_loader(caller) == method_loader(resolved) &&
      std::strcmp(method_descriptor(caller), method_descriptor(resolved)) == 0;
}

void* Check(void* self, void* resolved, int type) {
  if (resolved && enabled.load(std::memory_order_acquire)) {
    const uint32_t flags = static_cast<abi::arm64::ArtMethodPrefix*>(resolved)->access_flags.load(
        std::memory_order_relaxed);
    const bool is_static = flags & 0x0008;  // modifiers.h: kAccStatic.
    if (is_static != (type == 0)) {  // invoke_type.h: kStatic.
      throw_exception(self ? self : current_thread(), "Ljava/lang/IncompatibleClassChangeError;",
                      "invoke kind changed by structural redefinition");
      return nullptr;
    }
    // External old sites cannot access a private declaration. The switch
    // selector below handles obsolete self calls before vtable/IMT selection.
    if ((flags & 0x0002) && (type == 2 || type == 3 || type == 4)) {
      throw_exception(self ? self : current_thread(), "Ljava/lang/IllegalAccessError;",
                      "virtual target became private after structural redefinition");
      return nullptr;
    }
  }
  return resolved;
}
// Set while ResolveUnchecked runs on this thread: the fixed Android 11--15
// ResolveMethod<kNoChecks>(Thread*, ...) continues into the hooked cache
// overload on a DEX cache miss, which must not apply Check for the caller.
thread_local bool unchecked_resolution = false;
// Resolution without ART's invoke-kind checks, as the fixed sources use it:
// Android 16--17 ResolveMethodId, Android 11--15 ResolveMethod<kNoChecks>.
// May suspend; the caller roots any ObjPtr it keeps.
void* ResolveUnchecked(void* self, uint32_t index, void* caller, int type) {
  void* linker = class_linker.load(std::memory_order_acquire);
  if (resolve_method_id) return resolve_method_id(linker, index, caller);
  const bool enclosing = unchecked_resolution;
  unchecked_resolution = true;
  void* resolved = original_caller(linker, self, index, caller, type);
  unchecked_resolution = enclosing;
  return resolved;
}
void* ResolveCaller(void* linker, void* self, uint32_t index, void* caller, int type) noexcept {
  return Check(self, original_caller(linker, self, index, caller, type), type);
}
void* ResolveCache(void* linker, uint32_t index, Handle cache, Handle loader,
                   void* caller, int type) noexcept {
  void* resolved = original_cache(linker, index, cache, loader, caller, type);
  return unchecked_resolution ? resolved : Check(nullptr, resolved, type);
}
uintptr_t GetMethod(void* self, void* caller, const uint16_t* instruction) noexcept {
  if (enabled.load(std::memory_order_acquire)) {
    // dex_instruction_list.h and invoke_type.h, Android 11--17. Both 35c and
    // 3rc put method_idx in the second code unit. No object layout is needed.
    const uint8_t opcode = *instruction & 0xff;
    int type;
    switch (opcode) {
      case 0x6e: case 0x74: type = 2; break;  // virtual
      case 0x6f: case 0x75: type = 3; break;  // super
      case 0x70: case 0x76: type = 1; break;  // direct
      case 0x71: case 0x77: type = 0; break;  // static
      case 0x72: case 0x78: type = 4; break;  // interface
      default: return original_nterp(self, caller, instruction);
    }
    void* resolved = Check(self, ResolveUnchecked(self, instruction[1], caller, type), type);
    // Resolution or Check has installed the exception. Do not let
    // nterp cache a direct method's index as a virtual dispatch slot.
    if (!resolved) return 0;
  }
  return original_nterp(self, caller, instruction);
}
template <int type>
void* SelectMethod(void* self, void* caller, uintptr_t* receiver,
                   const uint16_t* instruction, bool lookup_only, bool* string_init) noexcept {
  Select original = type == 2 ? original_virtual : original_interface;
  if (!enabled.load(std::memory_order_acquire) || !scope_verified.load(std::memory_order_acquire) ||
      !class_verified.load(std::memory_order_acquire) || !(static_cast<abi::arm64::ArtMethodPrefix*>(caller)->access_flags.load(std::memory_order_relaxed) &
        abi::arm64::kAccObsoleteMethod))
    return original(self, caller, receiver, instruction, lookup_only, string_init);
  // Match ART's lookup-only contract: never trigger resolution on this path.
  if (lookup_only) return nullptr;
  void* resolved;
  {
    // Resolution may suspend. Preserve and refresh the exact ObjPtr local
    // owned by ART's DoInvoke, using its probed HandleScope representation.
    runtime_roots::Root root(self, *scope_thread_layout, *scope_layout, receiver);
    resolved = ResolveUnchecked(self, instruction[1], caller, type);
  }
  if (!resolved) return nullptr;
  const auto flags = static_cast<abi::arm64::ArtMethodPrefix*>(resolved)->access_flags.load(std::memory_order_relaxed);
  if ((flags & 0x0002) && !(flags & 0x0008) && OwnClass(caller, resolved)) {
    if (!*receiver) {
      throw_exception(self, "Ljava/lang/NullPointerException;", "null receiver of obsolete private self call");
      return nullptr;
    }
    // JBR selects the new private body directly, including on an overriding
    // subclass receiver. A direct method index must never enter the vtable/IMT.
    return resolved;
  }
  if (!Check(self, resolved, type)) return nullptr;
  return original(self, caller, receiver, instruction, lookup_only, string_init);
}

}  // namespace

bool Prepare(std::vector<Hook>* hooks) {
  using namespace symbols::invoke_kind;
  std::vector<Hook> prepared;
  current_thread = reinterpret_cast<decltype(current_thread)>(symbols::Find(symbols::runtime_thread::kCurrentThread));
  throw_exception = reinterpret_cast<decltype(throw_exception)>(symbols::Find(symbols::structural::kThrowNewException));
  if (!current_thread || !throw_exception) return false;
  resolve_method_id = reinterpret_cast<MethodId>(symbols::Find(kResolveMethodId));
  void* caller = symbols::Find(kResolveCaller);
  void* cache = symbols::Find(kResolveCache);
  if ((caller == nullptr) != (cache == nullptr) || (!resolve_method_id && !caller)) return false;
  if (caller) {
    prepared.push_back({caller, HookAddress(ResolveCaller), HookSlot(&original_caller), kResolveCaller.name});
    prepared.push_back({cache, HookAddress(ResolveCache), HookSlot(&original_cache), kResolveCache.name});
  }
  if (void* nterp = symbols::Find(kNterpGetMethod))
    prepared.push_back({nterp, HookAddress(GetMethod), HookSlot(&original_nterp), kNterpGetMethod.name});
  else if (resolve_method_id) return false;
  // The self-call capability is independent of the older invocation guards.
  // Missing or partial selection services retain conservative admission 71.
  void* select_virtual = symbols::Find(kSelectVirtual);
  void* select_interface = symbols::Find(kSelectInterface);
  method_descriptor = reinterpret_cast<decltype(method_descriptor)>(symbols::Find(kMethodDescriptor));
  method_loader = reinterpret_cast<decltype(method_loader)>(symbols::Find(kMethodLoader));
  scope_visitors.unsigned_ids = reinterpret_cast<abi::arm64::HandleScopeVisitors::Unsigned>(
      symbols::Find(symbols::interface_redefinition::kHandleScopeVisitRoots));
  void* cache_read = symbols::Find(kObsoleteCacheRead);
  void* cache_raw = symbols::Find(kObsoleteCacheRaw);
  mark = reinterpret_cast<decltype(mark)>(symbols::Find(symbols::runtime_thread::kReadBarrierMark));
  use_read_barrier = static_cast<const bool*>(symbols::Find(symbols::runtime_thread::kUseReadBarrier));
  // Android 14--16 share FindMethodToCall<type>; 14--15 resolve through the
  // hooked ResolveMethod<kNoChecks> original instead of ResolveMethodId.
  if ((resolve_method_id || caller) && select_virtual && select_interface && method_descriptor && method_loader &&
      scope_visitors.unsigned_ids && cache_read && cache_raw && mark && use_read_barrier) {
    prepared.push_back({cache_read, HookAddress(CopiedObsoleteCache<true>), HookSlot(&original_cache_read), kObsoleteCacheRead.name});
    prepared.push_back({cache_raw, HookAddress(CopiedObsoleteCache<false>), HookSlot(&original_cache_raw), kObsoleteCacheRaw.name});
    prepared.push_back({select_virtual, HookAddress(SelectMethod<2>), HookSlot(&original_virtual), kSelectVirtual.name});
    prepared.push_back({select_interface, HookAddress(SelectMethod<4>), HookSlot(&original_interface), kSelectInterface.name});
  }
  hooks->insert(hooks->end(), prepared.begin(), prepared.end());
  return true;
}
void KeepInterpreted(void* method, uint32_t precompiled_flag) {
  auto& flags = static_cast<abi::arm64::ArtMethodPrefix*>(method)->access_flags;
  const uint32_t before = flags.load(std::memory_order_relaxed);
  // IsCompilable ignores DontBother for intrinsics/precompiled methods. Native
  // and abstract methods have no bytecode to preserve in the interpreter.
  if (!(before & (0x80000000 | 0x0100 | 0x0400)))
    flags.store((before & ~precompiled_flag) | abi::arm64::kAccCompileDontBother, std::memory_order_relaxed);
}
void ObserveScope(void* thread, const abi::arm64::RuntimeThread* thread_layout,
                  const abi::arm64::InterfaceRuntime* layout, Handle target) {
  if (!original_virtual || !original_interface || scope_verified.load(std::memory_order_acquire)) return;
  if (!runtime_roots::VerifyScopes(*layout, *thread_layout, thread, target, scope_visitors)) return;
  std::lock_guard<std::mutex> lock(scope_mutex);
  if (scope_verified.load(std::memory_order_relaxed)) return;
  scope_thread_layout = thread_layout;
  scope_layout = layout;
  scope_verified.store(true, std::memory_order_release);
}
void ObserveClassLayout(size_t access_flags, size_t dex_cache, size_t ext_data, size_t obsolete_methods) {
  std::lock_guard<std::mutex> lock(scope_mutex);
  if (class_verified.load(std::memory_order_relaxed)) return;
  class_access = access_flags;
  class_dex_cache = dex_cache;
  class_ext_data = ext_data;
  ext_obsolete_methods = obsolete_methods;
  class_verified.store(true, std::memory_order_release);
}
bool SelectionServices() {
  return installed.load(std::memory_order_acquire) && original_virtual && original_interface;
}
bool SelfCalls() {
  return installed.load(std::memory_order_acquire) && scope_verified.load(std::memory_order_acquire) &&
      class_verified.load(std::memory_order_acquire);
}
bool Protecting() { return enabled.load(std::memory_order_acquire); }
void Installed(bool value) { installed.store(value, std::memory_order_release); }
void ObserveClassLinker(void* linker) { class_linker.store(linker, std::memory_order_release); }
bool Enable() {
  if (!installed.load(std::memory_order_acquire) || !class_linker.load(std::memory_order_acquire)) return false;
  enabled.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::invoke_kind
