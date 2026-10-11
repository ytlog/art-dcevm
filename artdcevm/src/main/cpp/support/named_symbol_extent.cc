#include "support/named_symbol_extent.h"
#include <shadowhook.h>
// Added by the small overlay to the same official ShadowHook/xDL instance.
extern "C" int shadowhook_get_named_function_extent(void*, const char*, const void*,
                                                    uintptr_t*, size_t*);
namespace artdcevm::named_symbol_extent {
bool Service::Prepare(const char* module, const char* name) {
  begin_ = end_ = 0;
  if (module == nullptr || name == nullptr) return false;
  void* handle = shadowhook_dlopen(module);
  if (handle == nullptr) return false;
  void* entry = shadowhook_dlsym(handle, name);
  uintptr_t begin = 0;
  size_t size = 0;
  const bool valid = entry != nullptr &&
      shadowhook_get_named_function_extent(handle, name, entry, &begin, &size) == 0 &&
      begin != 0 && size != 0 && size <= UINTPTR_MAX - begin;
  shadowhook_dlclose(handle);
  if (!valid) return false;
  begin_ = begin;
  end_ = begin + size;
  return true;
}
}  // namespace artdcevm::named_symbol_extent
