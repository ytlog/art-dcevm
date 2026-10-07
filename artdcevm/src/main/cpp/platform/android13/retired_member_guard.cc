#include "platform/android13/retired_member_guard.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace artdcevm::android13 {
namespace {
RetiredMemberGuardFunctions guard_functions{};
bool Guard(void* member, void* thread, void* result, ContainsRetiredMember contains,
           const char* descriptor, const char* message) {
  if (!contains(member)) return false;
  // JValue is an eight-byte union in the fixed Android 13 declaration. Copying
  // bytes also permits callers to supply opaque or unaligned result storage.
  if (result != nullptr) {
    const uint64_t zero = 0;
    std::memcpy(result, &zero, sizeof(zero));
  }
  if (thread == nullptr) thread = guard_functions.current_thread();
  guard_functions.throw_exception(thread, descriptor, message);
  return true;
}
}  // namespace
bool ConfigureRetiredMemberGuard(const RetiredMemberGuardFunctions& functions) {
  if (!functions.current_thread || !functions.throw_exception || !functions.deliver_exception ||
      !functions.contains_method || !functions.contains_field) return false;
  guard_functions = functions;
  return true;
}
bool GuardRetiredMethod(void* method, void* thread, void* result) {
  return Guard(method, thread, result, guard_functions.contains_method,
               "Ljava/lang/NoSuchMethodError;", "Method removed by ART DCEVM");
}
bool GuardRetiredField(void* field, void* thread, void* result) {
  return Guard(field, thread, result, guard_functions.contains_field,
               "Ljava/lang/NoSuchFieldError;", "Field removed by ART DCEVM");
}
[[noreturn]] void DeliverRetiredMemberException(void* thread) {
  guard_functions.deliver_exception(thread);
  std::abort();  // ART's declared NO_RETURN function cannot return normally.
}
}  // namespace artdcevm::android13
