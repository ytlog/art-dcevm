#pragma once
#include <jvmti.h>
#include <new>

namespace artdcevm {
// One inline hook on a named ART function: `replacement` runs instead of
// `target`, and `*original` receives the trampoline to the original code.
struct Hook {
  void* target;
  void* replacement;
  void** original;
  const char* name;
};

template <typename F> void* HookAddress(F function) { return reinterpret_cast<void*>(function); }
template <typename F> void** HookSlot(F* function) { return reinterpret_cast<void**>(function); }

// Call only from a catch handler. Error conversion must not itself allocate.
inline jvmtiError ArtHookExceptionError() noexcept {
  try { throw; }
  catch (const std::bad_alloc&) { return JVMTI_ERROR_OUT_OF_MEMORY; }
  catch (...) { return JVMTI_ERROR_INTERNAL; }
}
}  // namespace artdcevm
