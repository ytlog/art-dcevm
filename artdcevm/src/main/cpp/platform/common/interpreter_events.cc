#include "platform/common/interpreter_events.h"
#include "platform/common/art_hook.h"
#include "platform/common/continuation_profile.h"
#include "platform/common/private_continuation.h"
#include "platform/common/bound_method_handle.h"
#include "platform/symbols/bound_method_handle_symbols.h"
#include "shadowhook.h"
#include <mutex>
namespace artdcevm::interpreter_events {
namespace {
std::mutex mutex;
Pc originals[5]{};
void* targets[5]{};
void* hooks[5]{};
Move move = nullptr;
void* move_hook = nullptr;
bool ExceptionEvent(void* self, void* frame, bool skip, bool skip_throw) {
  bool handled = false;
  const bool value = private_continuation::Exception(self, frame, skip, skip_throw, &handled);
  return handled ? value : move(self, frame, skip, skip_throw);
}
template <size_t i>
bool Event(void* self, const void* accessor, const void* frame, uint32_t pc,
           const void* instrumentation, void* result) {
  const bool passed = bound_method_handle::PcRequired(frame, pc)
      ? bound_method_handle::DispatchPc(originals[i], self, accessor, frame, pc, instrumentation, result)
      : continuation_profile::Pc(originals[i], self, accessor, frame, pc, instrumentation, result);
  if constexpr (i != 4) {
    if (passed && private_continuation::PcRequired(accessor, frame, pc))
      return private_continuation::Event(self, accessor, frame, pc, instrumentation, result, true);
  }
  return passed;
}
}
bool Prepare() {
  std::lock_guard<std::mutex> lock(mutex);
  using namespace symbols::bound_method_handle;
  const symbols::NamedSymbol names[] = {kDexPc00,kDexPc01,kDexPc10,kDexPc11,kDexPcModern};
  void* replacements[] = {HookAddress(Event<0>),HookAddress(Event<1>),HookAddress(Event<2>),HookAddress(Event<3>),HookAddress(Event<4>)};
  for (size_t i = 0; i < 5; ++i) if (!hooks[i]) {
    targets[i] = symbols::Find(names[i]);
    if (!targets[i]) continue;
    for (size_t j = 0; j < i; ++j) if (targets[i] == targets[j] && hooks[j]) {
      hooks[i] = hooks[j]; originals[i] = originals[j]; break;
    }
    if (!hooks[i]) hooks[i] = shadowhook_hook_sym_addr(targets[i], replacements[i], HookSlot(&originals[i]));
  }
  if (!move_hook) if (void* entry = symbols::Find(kMoveException))
    move_hook = shadowhook_hook_sym_addr(entry, HookAddress(ExceptionEvent), HookSlot(&move));
  return hooks[4] || (hooks[0] && hooks[1] && hooks[2] && hooks[3]);
}
bool Modern() { return hooks[4] != nullptr; }
bool ExceptionService() { return move_hook && move; }
bool MoveException(void* self, void* frame, bool skip, bool skip_throw) {
  return move(self, frame, skip, skip_throw);
}
}
