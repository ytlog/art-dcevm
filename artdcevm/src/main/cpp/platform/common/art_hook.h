#pragma once
#include <jvmti.h>
#include <new>

namespace artdcevm {
// Call only from a catch handler. Error conversion must not itself allocate.
inline jvmtiError ArtHookExceptionError() noexcept {
  try { throw; }
  catch (const std::bad_alloc&) { return JVMTI_ERROR_OUT_OF_MEMORY; }
  catch (...) { return JVMTI_ERROR_INTERNAL; }
}
}  // namespace artdcevm
