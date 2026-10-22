#include "platform/common/invoke_kind.h"
#include "platform/common/bound_method_handle.h"
#include "platform/common/captured_method_handle.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <mutex>
#include "shadowhook.h"
#include "platform/common/runtime_roots.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/fault_injection.h"
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
std::atomic<bool> class_kinds_enabled{false};
void* (*lookup_type)(void*, uint16_t, void*, void*) = nullptr;
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
ObsoleteCache named_cache_read = nullptr;
void* (*mark)(void*) = nullptr;
const bool* use_read_barrier = nullptr;
std::atomic<bool> class_verified{false};
size_t class_access = 0, class_dex_cache = 0, class_ext_data = 0, ext_obsolete_methods = 0;
size_t gc_marking_offset = ~size_t{0};
size_t method_index_offset = ~size_t{0};
size_t class_iftable = ~size_t{0};
abi::arm64::FrameReference frame_reference = nullptr;
size_t exception_offset = ~size_t{0};
abi::arm64::FramePc observed_frame_pc = nullptr;
abi::arm64::ArtField* (*find_instance_field)(void*, std::string_view, std::string_view) = nullptr;
bool (*descriptor_equals)(void*, const char*) = nullptr;
bool legacy_handle_refine = false;
void* handle_dispatch_target = nullptr;
void* handle_dispatch_hook = nullptr;
std::mutex handle_dispatch_mutex;
std::atomic<bool> interface_symbols_enabled{false};
using HandleDispatch = bool (*)(void*, void*, Handle, const abi::arm64::InstructionOperands*, uint64_t*);
using HandleRefine = void* (*)(void*, void*, const int&, Handle, Handle, uint32_t, void*);
HandleDispatch original_handle_dispatch = nullptr;
HandleRefine original_handle_refine = nullptr;
std::atomic<int64_t> handle_kind_offset{-1}, handle_target_offset{-1}, handle_type_offset{-1}, ptypes_offset{-1}, frame_refs_offset{-1}, bound_delegate_offset{-1}, bound_receiver_offset{-1};


template <bool read_barrier>
uintptr_t Reference(uint32_t reference) {
  if constexpr (read_barrier)
    if (reference && (use_read_barrier ? *use_read_barrier :
        (gc_marking_offset != ~size_t{0} && runtime_roots::Field<uint32_t>(current_thread(), gc_marking_offset))))
      return reinterpret_cast<uintptr_t>(mark(reinterpret_cast<void*>(static_cast<uintptr_t>(reference))));
  return reference;
}

template <bool read_barrier>
uintptr_t CopiedObsoleteCache(void* method) noexcept {
  const auto* prefix = static_cast<abi::arm64::ArtMethodPrefix*>(method);
  constexpr uint32_t kCopiedObsolete = 0x01000000 | abi::arm64::kAccObsoleteMethod;
  if (class_verified.load(std::memory_order_acquire) &&
      (prefix->access_flags.load(std::memory_order_relaxed) & kCopiedObsolete) == kCopiedObsolete) {
    // Class::Alloc creates nonmoving classes. A structural copied method keeps
    // its interface's DEX identity even when only the implementation changes:
    // a raw JNI ID can therefore be obsolete while its declaring interface is live.
    void* declaring = reinterpret_cast<void*>(static_cast<uintptr_t>(prefix->declaring_class));
    if (runtime_roots::Field<uint32_t>(declaring, class_access) &
        (0x00200000 | evolution::kAccInterface)) { // kAccObsoleteObject or retained interface.
      // A frame from an earlier non-structural redefinition can still be in
      // ART's normal obsolete map. Preserve that historical DEX selection.
      void* ext = reinterpret_cast<void*>(Reference<read_barrier>(runtime_roots::Field<uint32_t>(declaring, class_ext_data)));
      const auto* methods = ext ? reinterpret_cast<const abi::arm64::PointerArray*>(
          Reference<read_barrier>(runtime_roots::Field<uint32_t>(ext, ext_obsolete_methods))) : nullptr;
      for (int32_t i = 0; methods && i < methods->length; ++i)
        if (methods->elements[i] == reinterpret_cast<uintptr_t>(method))
          return (read_barrier ? (original_cache_read ? original_cache_read : named_cache_read) : original_cache_raw)(method);
      // A structural copy lives outside the interface's method/obsolete arrays,
      // but its code still belongs to that interface's DEX.
      // Match Class::GetDexCache's CC/CMC and no-barrier behavior.
      return Reference<read_barrier>(runtime_roots::Field<uint32_t>(declaring, class_dex_cache));
    }
  }
  return (read_barrier ? (original_cache_read ? original_cache_read : named_cache_read) : original_cache_raw)(method);
}

// Find field offsets through the named ART declaration service, as the
// retirement guards do. No MethodHandle/MethodType object offsets are guessed.
int64_t HandleField(void* object, const char* name, const char* descriptor, std::atomic<int64_t>& cached) {
  int64_t offset = cached.load(std::memory_order_acquire);
  if (offset < 0) {
    void* klass = reinterpret_cast<void*>(Reference<true>(static_cast<abi::arm64::Object*>(object)->klass));
    const auto* field = find_instance_field(klass, name, descriptor);
    if (!field) return -1;
    offset = field->offset;
    cached.store(offset, std::memory_order_release);
  }
  return offset;
}
void* HandleReceiverType(void* type) {
  if (!type) return nullptr;
  int64_t offset = HandleField(type, "ptypes", "[Ljava/lang/Class;", ptypes_offset);
  const auto* params = offset < 0 ? nullptr : reinterpret_cast<const abi::arm64::ObjectArray*>(
      Reference<true>(runtime_roots::Field<uint32_t>(type, offset)));
  return params && params->length ? reinterpret_cast<void*>(Reference<true>(params->elements[0])) : nullptr;
}
bool RejectInterfaceReceiver(void* self, uint32_t reference, void* target, void* receiver_type) {
  if (!reference || !target || !receiver_type) return false;  // ART retains its null/error ordering.
  auto* method = static_cast<abi::arm64::ArtMethodPrefix*>(target);
  void* declaring = reinterpret_cast<void*>(Reference<true>(method->declaring_class));
  if (!(runtime_roots::Field<uint32_t>(declaring, class_access) & evolution::kAccInterface)) return false;
  void* receiver = reinterpret_cast<void*>(static_cast<uintptr_t>(reference));
  void* klass = reinterpret_cast<void*>(Reference<true>(static_cast<abi::arm64::Object*>(receiver)->klass));
  const auto* table = reinterpret_cast<const abi::arm64::ObjectArray*>(
      Reference<true>(runtime_roots::Field<uint32_t>(klass, class_iftable)));
  for (int32_t i = 0; table && i + 1 < table->length; i += 2)
    if (Reference<true>(table->elements[i]) == reinterpret_cast<uintptr_t>(declaring)) return false;
  // RefineTargetMethod would use reflection's VerifyObjectIsClass and throw
  // IllegalArgumentException. A linked interface handle instead fails dispatch
  // with ICCE on JBR; a class-owned virtual handle has lost its slot (AME).
  // ART releases choose handle kinds differently for inherited defaults. The
  // original receiver type preserves the symbolic class/interface convention.
  // Argument conversions have already run at this boundary.
  const bool interface_owner = runtime_roots::Field<uint32_t>(receiver_type, class_access) & evolution::kAccInterface;
  throw_exception(self, interface_owner ? "Ljava/lang/IncompatibleClassChangeError;" : "Ljava/lang/AbstractMethodError;",
                  "MethodHandle receiver lost the declaring interface");
  return true;
}
bool DispatchHandle(void* self, void* frame, Handle handle,
                    const abi::arm64::InstructionOperands* operands, uint64_t* result) noexcept {
  if (interface_symbols_enabled.load(std::memory_order_acquire)) {
    void* object = handle.Get<void>();
    int64_t kind = HandleField(object, "handleKind", "I", handle_kind_offset);
    int64_t target = HandleField(object, "artFieldOrMethod", "J", handle_target_offset);
    int64_t type = HandleField(object, "type", "Ljava/lang/invoke/MethodType;", handle_type_offset);
    if (kind >= 0 && target >= 0 && type >= 0) {
      const int value = runtime_roots::Field<int>(object, kind);
      if ((value == 0 || value == 4) && operands->num_operands &&
          RejectInterfaceReceiver(self, frame_reference(frame, operands->GetOperand(0)),
              reinterpret_cast<void*>(runtime_roots::Field<uintptr_t>(object, target)), HandleReceiverType(
                  reinterpret_cast<void*>(Reference<true>(runtime_roots::Field<uint32_t>(object, type)))))) {
        if (result) *result = 0;
        return false;
      }
    }
  }
  return original_handle_dispatch(self, frame, handle, operands, result);
}
void* RefineHandle(void* self, void* frame, const int& kind, Handle type, Handle callsite,
                   uint32_t reg, void* target) noexcept {
  if (interface_symbols_enabled.load(std::memory_order_acquire) && (kind == 0 || kind == 4)) {
    uint32_t receiver = frame_reference(frame, reg);
    // Android 11--12's RefineTargetMethod unwraps a caller-transformer frame
    // here, before checking its receiver. Match that fixed-source decision.
    int64_t offset = HandleField(callsite.Get<void>(), "ptypes", "[Ljava/lang/Class;", ptypes_offset);
    const auto* params = offset < 0 ? nullptr : reinterpret_cast<const abi::arm64::ObjectArray*>(
        Reference<true>(runtime_roots::Field<uint32_t>(callsite.Get<void>(), offset)));
    if (receiver && params && params->length == 1 && descriptor_equals(
          reinterpret_cast<void*>(Reference<true>(params->elements[0])), "Ldalvik/system/EmulatedStackFrame;")) {
      void* emulated = reinterpret_cast<void*>(static_cast<uintptr_t>(receiver));
      offset = HandleField(emulated, "references", "[Ljava/lang/Object;", frame_refs_offset);
      const auto* refs = offset < 0 ? nullptr : reinterpret_cast<const abi::arm64::ObjectArray*>(
          Reference<true>(runtime_roots::Field<uint32_t>(emulated, offset)));
      receiver = refs && refs->length ? static_cast<uint32_t>(Reference<true>(refs->elements[0])) : 0;
    }
    if (RejectInterfaceReceiver(self, receiver, target, HandleReceiverType(type.Get<void>()))) return nullptr;
  }
  return original_handle_refine(self, frame, kind, type, callsite, reg, target);
}

bool OwnClass(void* caller, void* resolved) {
  // Class identity includes the defining loader. Obsolete ART class objects
  // deliberately survive reference replacement, so pointer equality is insufficient.
  return method_loader(caller) == method_loader(resolved) &&
      std::strcmp(method_descriptor(caller), method_descriptor(resolved)) == 0;
}

// ResolveMethod has already resolved the symbolic owner, so this lookup never
// loads a class. Read the caller's own (possibly obsolete) DEX, not the resolved
// method's declaring class: a virtual class call can resolve a copied default,
// and an interface call can resolve Object. Both are legal ART/JBR cases.
void* MethodCache(void* caller) {
  auto* method = static_cast<abi::arm64::ArtMethodPrefix*>(caller);
  if (method->access_flags.load(std::memory_order_relaxed) & abi::arm64::kAccObsoleteMethod)
    return reinterpret_cast<void*>(CopiedObsoleteCache<true>(caller));
  void* declaring = reinterpret_cast<void*>(Reference<true>(method->declaring_class));
  return reinterpret_cast<void*>(Reference<true>(runtime_roots::Field<uint32_t>(declaring, class_dex_cache)));
}
void* SymbolicOwner(void* cache, uint32_t index, void* loader, bool* valid) {
  *valid = false;
  if (!cache) return nullptr;
  const auto* dex = reinterpret_cast<const abi::arm64::DexFilePrefix*>(static_cast<abi::arm64::DexCache*>(cache)->dex_file);
  const auto view = DexViewOf(*dex);
  // dex_file.h Header/MethodId, identical in StandardDex and CompactDex.
  uint32_t count = 0, offset = 0;
  if (view.size >= 0x70) {
    std::memcpy(&count, view.begin + 88, sizeof(count));
    std::memcpy(&offset, view.begin + 92, sizeof(offset));
  }
  const uint64_t at = uint64_t{offset} + uint64_t{index} * 8;
  if (index >= count || at + 8 > view.size) {
    return nullptr;
  }
  *valid = true;
  uint16_t owner = 0;
  std::memcpy(&owner, view.begin + at, sizeof(owner));
  return lookup_type(class_linker.load(std::memory_order_acquire), owner, cache, loader);
}
bool CheckClassKind(void* self, void* caller, uint32_t index, int type,
                    void* supplied_cache = nullptr, void* supplied_loader = nullptr) {
  if (!class_kinds_enabled.load(std::memory_order_acquire) || (type != 2 && type != 4)) return true;
  void* cache = supplied_cache ? supplied_cache : MethodCache(caller);
  void* loader = supplied_cache ? supplied_loader : cache ? reinterpret_cast<void*>(
      Reference<true>(static_cast<abi::arm64::DexCache*>(cache)->class_loader)) : nullptr;
  bool valid = false;
  void* klass = SymbolicOwner(cache, index, loader, &valid);
  if (!valid) {
    throw_exception(self ? self : current_thread(), "Ljava/lang/VerifyError;", "invalid symbolic method owner");
    return false;
  }
  if (!klass || (bool(runtime_roots::Field<uint32_t>(klass, class_access) & evolution::kAccInterface) != (type == 4))) {
    throw_exception(self ? self : current_thread(), "Ljava/lang/IncompatibleClassChangeError;",
                    "symbolic owner changed between class and interface");
    return false;
  }
  return true;
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
  void* resolved = original_caller(linker, self, index, caller, type);
  if (resolved && !unchecked_resolution && !CheckClassKind(self, caller, index, type)) return nullptr;
  return Check(self, resolved, type);
}
void* ResolveCache(void* linker, uint32_t index, Handle cache, Handle loader,
                   void* caller, int type) noexcept {
  void* resolved = original_cache(linker, index, cache, loader, caller, type);
  if (resolved && !unchecked_resolution && !CheckClassKind(nullptr, caller, index, type, cache.Get<void>(), loader.Get<void>())) return nullptr;
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
    void* resolved = ResolveUnchecked(self, instruction[1], caller, type);
    if (resolved && !CheckClassKind(self, caller, instruction[1], type)) return 0;
    if (resolved && type == 1 && resolve_method_id && method_descriptor && method_loader &&
        class_verified.load(std::memory_order_acquire) &&
        (static_cast<abi::arm64::ArtMethodPrefix*>(caller)->access_flags.load(std::memory_order_relaxed) &
         abi::arm64::kAccObsoleteMethod) &&
        (static_cast<abi::arm64::ArtMethodPrefix*>(resolved)->access_flags.load(std::memory_order_relaxed) &
         (evolution::kAccPrivate | evolution::kAccStatic)) == evolution::kAccPrivate && OwnClass(caller, resolved)) {
      // Fixed Android 16--17 NterpGetMethod encodes invoke-direct as the
      // ArtMethod pointer (low bit only marks String construction). Returning
      // the self target avoids obsolete-Class private access checks. ART's
      // caller already roots the receiver across this suspending entry and
      // performs its normal null check after decoding the result. Leave the
      // TLS cache empty rather than duplicating its private implementation.
      return reinterpret_cast<uintptr_t>(resolved);
    }
    resolved = Check(self, resolved, type);
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
  if (!CheckClassKind(self, caller, instruction[1], type)) return nullptr;
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
  find_instance_field = reinterpret_cast<decltype(find_instance_field)>(symbols::Find(symbols::structural::kFindInstanceField));
  descriptor_equals = reinterpret_cast<decltype(descriptor_equals)>(symbols::Find(kClassDescriptorEquals));
  // Resolve names now; activation waits for the first accepted symbolic change.
  // RefineTargetMethod's older ABI has one extra rooted method-type argument.
  for (const auto& symbol : {kHandleDispatch0, kHandleDispatch1, kHandleDispatch2,
                            kHandleDispatch3, kHandleDispatch4, kHandleDispatch5})
    if ((handle_dispatch_target = symbols::Find(symbol))) break;
  if (!handle_dispatch_target) {
    handle_dispatch_target = symbols::Find(kHandleRefineLegacy);
    legacy_handle_refine = true;
  }
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
  // A missing selector retains admission 71 only for its target kind. Some
  // fixed-source 14/17 modules retain the interface selector but inline virtual.
  void* select_virtual = symbols::Find(kSelectVirtual);
  void* select_interface = symbols::Find(kSelectInterface);
  const int selection_mask = ARTDCEVM_PRIVATE_SELF_MASK();
  if (selection_mask & 1) select_virtual = nullptr;
  if (selection_mask & 2) select_interface = nullptr;
  method_descriptor = reinterpret_cast<decltype(method_descriptor)>(symbols::Find(kMethodDescriptor));
  method_loader = reinterpret_cast<decltype(method_loader)>(symbols::Find(kMethodLoader));
  lookup_type = reinterpret_cast<decltype(lookup_type)>(symbols::Find(symbols::interface_redefinition::kLookupCache));
  scope_visitors.unsigned_ids = reinterpret_cast<abi::arm64::HandleScopeVisitors::Unsigned>(
      symbols::Find(symbols::interface_redefinition::kHandleScopeVisitRoots));
  void* cache_legacy = symbols::Find(kObsoleteCacheLegacy);
  void* cache_read = symbols::Find(kObsoleteCacheRead);
  void* cache_raw = symbols::Find(kObsoleteCacheRaw);
  named_cache_read = reinterpret_cast<ObsoleteCache>(cache_read ? cache_read : cache_legacy);
  mark = reinterpret_cast<decltype(mark)>(symbols::Find(symbols::runtime_thread::kReadBarrierMark));
  use_read_barrier = static_cast<const bool*>(symbols::Find(symbols::runtime_thread::kUseReadBarrier));
  // Profile Saver also reads copied obsolete methods. Older ART has the
  // same DEX ownership rule without switch-selector services or templates.
  // ART 11--13's non-template getter uses the build-time read barrier. Most
  // modules do not export the later collector-selection flag. Reference()
  // already uses the verified Thread marking declaration in that case; the
  // Class proof gates all private reads until that declaration is available.
  if (cache_legacy && !cache_read && !cache_raw && mark)
    prepared.push_back({cache_legacy, HookAddress(CopiedObsoleteCache<true>),
                        HookSlot(&original_cache_read), kObsoleteCacheLegacy.name});
  // Obsolete copied defaults need their retained DEX even when an invoke-kind
  // request is rejected. Cache ownership does not depend on callee selection.
  if (cache_read && cache_raw && mark && use_read_barrier) {
    prepared.push_back({cache_read, HookAddress(CopiedObsoleteCache<true>), HookSlot(&original_cache_read), kObsoleteCacheRead.name});
    prepared.push_back({cache_raw, HookAddress(CopiedObsoleteCache<false>), HookSlot(&original_cache_raw), kObsoleteCacheRaw.name});
  }
  // Android 14--17 share FindMethodToCall<type>; 14--15 resolve through the
  // hooked ResolveMethod<kNoChecks> original instead of ResolveMethodId.
  if ((resolve_method_id || caller) && (select_virtual || select_interface) && method_descriptor && method_loader &&
      scope_visitors.unsigned_ids && cache_read && cache_raw && mark && use_read_barrier) {
    if (select_virtual)
      prepared.push_back({select_virtual, HookAddress(SelectMethod<2>), HookSlot(&original_virtual), kSelectVirtual.name});
    if (select_interface)
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
  if ((!original_virtual && !original_interface) || scope_verified.load(std::memory_order_acquire)) return;
  if (!runtime_roots::VerifyScopes(*layout, *thread_layout, thread, target, scope_visitors)) return;
  std::lock_guard<std::mutex> lock(scope_mutex);
  if (scope_verified.load(std::memory_order_relaxed)) return;
  scope_thread_layout = thread_layout;
  scope_layout = layout;
  scope_verified.store(true, std::memory_order_release);
}
void ObserveClassLayout(size_t access_flags, size_t dex_cache, size_t ext_data, size_t obsolete_methods, size_t gc_marking, size_t method_index, size_t iftable, abi::arm64::FrameReference reference, size_t exception, abi::arm64::FramePc frame_pc, abi::arm64::FrameReturn frame_return) {
  std::lock_guard<std::mutex> lock(scope_mutex);
  if (class_verified.load(std::memory_order_relaxed)) return;
  class_access = access_flags;
  class_dex_cache = dex_cache;
  class_ext_data = ext_data;
  ext_obsolete_methods = obsolete_methods;
  gc_marking_offset = gc_marking;
  method_index_offset = method_index;
  class_iftable = iftable;
  frame_reference = reference;
  exception_offset = exception;
  observed_frame_pc = frame_pc;
  bound_method_handle::Configure(frame_pc, CheckBoundReceiver, frame_return);
  class_verified.store(true, std::memory_order_release);
}
bool SelectionServices() {
  return SelectionServices(false) && SelectionServices(true);
}
bool SelectionServices(bool interface_target) {
  return SelectionUnavailable(interface_target) == nullptr;
}
const char* SelectionUnavailable(bool interface_target) {
  if (!installed.load(std::memory_order_acquire)) return "invocation guards not installed";
  if (!(interface_target ? original_interface : original_virtual))
    return interface_target ? "named interface selector or its fixed-source dependencies unavailable"
                            : "named virtual selector or its fixed-source dependencies unavailable";
  return nullptr;
}
bool SelfCalls() {
  return SelfCalls(false) && SelfCalls(true);
}
bool SelfCalls(bool interface_target) {
  return SelectionServices(interface_target) && scope_verified.load(std::memory_order_acquire) &&
      class_verified.load(std::memory_order_acquire);
}
bool Protecting() { return enabled.load(std::memory_order_acquire); }
bool CopiedFrames() {
  return installed.load(std::memory_order_acquire) && class_verified.load(std::memory_order_acquire) &&
      original_cache_read && mark && (use_read_barrier || gc_marking_offset != ~size_t{0});
}
bool DirectSelectionServices() {
  return installed.load(std::memory_order_acquire) && resolve_method_id && original_nterp &&
      method_descriptor && method_loader;
}
bool PrivateDirectFrames() {
  return CopiedFrames() && DirectSelectionServices();
}
bool ClassKinds() {
  return ClassKindUnavailable() == nullptr;
}
const char* ClassKindUnavailable() {
  if (!installed.load(std::memory_order_acquire)) return "invocation guards not installed";
  if (!class_verified.load(std::memory_order_acquire)) return "declaring Class not probed";
  if (method_index_offset == ~size_t{0}) return "method index declaration not probed";
  if (!lookup_type) return "named resolved type lookup unavailable";
  if (!original_cache_read && !named_cache_read) return "named obsolete DexCache service unavailable";
  if (!mark || (!use_read_barrier && gc_marking_offset == ~size_t{0})) return "read barrier collector declaration unavailable";
  return nullptr;
}
bool InterfaceSymbols() {
  return ClassKinds() && exception_offset != ~size_t{0} && observed_frame_pc && frame_reference && class_iftable != ~size_t{0} && find_instance_field &&
      handle_dispatch_target && (!legacy_handle_refine || descriptor_equals);
}
static int InspectBoundReceiver(void* bound) {
  if (!bound) return 0;
  bool complete = true;
  const auto read = [&complete](void* object, const char* name, const char* signature, std::atomic<int64_t>& cached) -> void* {
    const auto offset = HandleField(object, name, signature, cached);
    if (offset < 0) complete = false;
    return offset < 0 ? nullptr : reinterpret_cast<void*>(Reference<true>(runtime_roots::Field<uint32_t>(object, offset)));
  };
  void* delegate = read(bound, "delegate", "Ljava/lang/invoke/MethodHandle;", bound_delegate_offset);
  void* receiver = read(bound, "receiver", "Ljava/lang/Object;", bound_receiver_offset);
  if (!complete) return 0;
  if (!delegate || !receiver) return 1;
  const auto kind = HandleField(delegate, "handleKind", "I", handle_kind_offset);
  if (kind < 0) return 0;
  const auto value = runtime_roots::Field<int>(delegate, kind);
  void* type = HandleReceiverType(read(delegate, "type", "Ljava/lang/invoke/MethodType;", handle_type_offset));
  if (!complete || !type) return 0;
  if (!(runtime_roots::Field<uint32_t>(type, class_access) & evolution::kAccInterface)) return 1;
  void* klass = reinterpret_cast<void*>(Reference<true>(static_cast<abi::arm64::Object*>(receiver)->klass));
  const auto* interfaces = reinterpret_cast<const abi::arm64::ObjectArray*>(
      Reference<true>(runtime_roots::Field<uint32_t>(klass, class_iftable)));
  for (int32_t i = 0; interfaces && i + 1 < interfaces->length; i += 2)
    if (Reference<true>(interfaces->elements[i]) == reinterpret_cast<uintptr_t>(type)) return 1;
  if (value != 0 && value != 4) return 3;  // Continue the graph before deciding whether it dispatches.
  const auto target_offset = HandleField(delegate, "artFieldOrMethod", "J", handle_target_offset);
  const auto* target = target_offset < 0 ? nullptr : reinterpret_cast<abi::arm64::ArtMethodPrefix*>(
      runtime_roots::Field<uintptr_t>(delegate, target_offset));
  if (target_offset < 0) return 0;
  if (!target || (target->access_flags.load(std::memory_order_relaxed) & abi::arm64::kAccObsoleteMethod)) return 1;
  void* declaring = reinterpret_cast<void*>(Reference<true>(target->declaring_class));
  if (!(runtime_roots::Field<uint32_t>(declaring, class_access) & evolution::kAccInterface)) return 1;
  return 2;
}
int CheckBoundReceiver(void* self, void* bound) {
  if (runtime_roots::Field<void*>(self, exception_offset)) return false;
  const int result = InspectBoundReceiver(bound);
  if (result == 1) return 0;
  if (result == 3) {
    if (captured_method_handle::Complete()) return 2;
    if (!runtime_roots::Field<void*>(self, exception_offset))
      throw_exception(self, "Ljava/lang/InternalError;", "Captured MethodHandle continuation failed");
    return 1;
  }
  throw_exception(self, result == 2 ? "Ljava/lang/IncompatibleClassChangeError;" : "Ljava/lang/InternalError;",
                  result == 2 ? "MethodHandle receiver lost the declaring interface" : "Bound MethodHandle receiver inspection failed");
  return true;
}
bool EnableInterfaceSymbols() {
  if (!InterfaceSymbols() || !Enable()) return false;
  std::lock_guard<std::mutex> lock(handle_dispatch_mutex);
  if (!handle_dispatch_hook) {
    handle_dispatch_hook = legacy_handle_refine
        ? shadowhook_hook_sym_addr(handle_dispatch_target, HookAddress(RefineHandle), HookSlot(&original_handle_refine))
        : shadowhook_hook_sym_addr(handle_dispatch_target, HookAddress(DispatchHandle), HookSlot(&original_handle_dispatch));
    if (!handle_dispatch_hook) return false;
  }
  interface_symbols_enabled.store(true, std::memory_order_release);
  bound_method_handle::Enable();
  return true;
}
bool InterfaceResolution(void* cache, uint32_t index) {
  if (!ClassKinds() || !cache) return false;
  void* loader = reinterpret_cast<void*>(Reference<true>(static_cast<abi::arm64::DexCache*>(cache)->class_loader));
  bool valid = false;
  void* owner = SymbolicOwner(cache, index, loader, &valid);
  return owner && (runtime_roots::Field<uint32_t>(owner, class_access) & evolution::kAccInterface);
}
bool EnableClassKinds() {
  if (!ClassKinds() || !Enable()) return false;
  class_kinds_enabled.store(true, std::memory_order_release);
  return true;
}
bool UninstantiableConstructor(const void* method) {
  if (!class_kinds_enabled.load(std::memory_order_acquire) || !method ||
      !(static_cast<const abi::arm64::ArtMethodPrefix*>(method)->access_flags.load(std::memory_order_relaxed) & evolution::kAccConstructor)) return false;
  void* cache = MethodCache(const_cast<void*>(method));
  if (!cache) return false;
  void* loader = reinterpret_cast<void*>(Reference<true>(static_cast<abi::arm64::DexCache*>(cache)->class_loader));
  uint32_t index = runtime_roots::Field<uint32_t>(const_cast<void*>(method), method_index_offset);
  bool valid = false;
  void* klass = SymbolicOwner(cache, index, loader, &valid);
  return klass && (runtime_roots::Field<uint32_t>(klass, class_access) & evolution::kAccAbstract);
}
bool ClassKindProtection() { return class_kinds_enabled.load(std::memory_order_acquire); }
void Installed(bool value) { installed.store(value, std::memory_order_release); }
void ObserveClassLinker(void* linker) { class_linker.store(linker, std::memory_order_release); }
bool Enable() {
  if (!installed.load(std::memory_order_acquire) || !class_linker.load(std::memory_order_acquire)) return false;
  enabled.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::invoke_kind
