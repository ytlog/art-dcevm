#include "platform/common/invoke_kind.h"
#include <atomic>
#include <cstdint>
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

void* Check(void* self, void* resolved, int type) {
  if (resolved && enabled.load(std::memory_order_acquire)) {
    const bool is_static = static_cast<abi::arm64::ArtMethodPrefix*>(resolved)->access_flags.load(
        std::memory_order_relaxed) & 0x0008;  // modifiers.h: kAccStatic.
    if (is_static != (type == 0)) {  // invoke_type.h: kStatic.
      throw_exception(self ? self : current_thread(), "Ljava/lang/IncompatibleClassChangeError;",
                      "invoke kind changed by structural redefinition");
      return nullptr;
    }
  }
  return resolved;
}
void* ResolveCaller(void* linker, void* self, uint32_t index, void* caller, int type) noexcept {
  return Check(self, original_caller(linker, self, index, caller, type), type);
}
void* ResolveCache(void* linker, uint32_t index, Handle cache, Handle loader,
                   void* caller, int type) noexcept {
  void* resolved = original_cache(linker, index, cache, loader, caller, type);
  return Check(nullptr, resolved, type);
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
    void* linker = class_linker.load(std::memory_order_acquire);
    void* resolved = resolve_method_id ? resolve_method_id(linker, instruction[1], caller)
        : original_caller(linker, self, instruction[1], caller, type);
    resolved = Check(self, resolved, type);
    // Resolution or Check has installed the exception. Do not let
    // nterp cache a static method's index as a virtual dispatch slot.
    if (!resolved) return 0;
  }
  return original_nterp(self, caller, instruction);
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
bool Protecting() { return enabled.load(std::memory_order_acquire); }
void Installed(bool value) { installed.store(value, std::memory_order_release); }
void ObserveClassLinker(void* linker) { class_linker.store(linker, std::memory_order_release); }
bool Enable() {
  if (!installed.load(std::memory_order_acquire) || !class_linker.load(std::memory_order_acquire)) return false;
  enabled.store(true, std::memory_order_release);
  return true;
}
}  // namespace artdcevm::invoke_kind
