#include "platform/android13/retired_member_guard.h"
#include "platform/android13/retired_method_registry.h"
#include "platform/android13/retired_field_registry.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace guard = artdcevm::android13;
namespace {
guard::RetiredMethodRegistry methods;
guard::RetiredFieldRegistry fields;
int method, field, live_member, current_thread, explicit_thread;
size_t current_calls = 0, throws = 0, deliveries = 0;
const char* expected_descriptor;
const char* expected_message;
void* expected_thread;
std::jmp_buf exception_jump;
void* Current() { ++current_calls; return &current_thread; }
void Throw(void* thread, const char* descriptor, const char* message) {
  assert(thread == expected_thread);
  assert(std::strcmp(descriptor, expected_descriptor) == 0);
  assert(std::strcmp(message, expected_message) == 0);
  ++throws;
}
void Deliver(void* thread) {
  assert(thread == expected_thread);
  ++deliveries;
  std::longjmp(exception_jump, 1);
}
bool ContainsMethod(void* member) { return methods.Contains(member); }
bool ContainsField(void* member) { return fields.TokenFor(member) != 0; }
bool AllocatorContains(const void* allocator, void* member) { return allocator == member; }
void CheckGuard(guard::RetiredMemberGuard check, void* member, const char* descriptor,
                const char* message) {
  expected_descriptor = descriptor;
  expected_message = message;
  // A live/null identity must preserve all result bytes and avoid thread lookup.
  unsigned char storage[10];
  std::memset(storage, 0xa5, sizeof(storage));
  const size_t before = current_calls;
  assert(!check(&live_member, nullptr, storage + 1));
  assert(!check(nullptr, nullptr, storage + 1));
  for (unsigned char byte : storage) assert(byte == 0xa5);
  assert(current_calls == before);

  expected_thread = &explicit_thread;
  assert(check(member, &explicit_thread, storage + 1));
  assert(current_calls == before);
  assert(storage[0] == 0xa5 && storage[9] == 0xa5);
  for (int index = 1; index <= 8; ++index) assert(storage[index] == 0);
  expected_thread = &current_thread;
  assert(check(member, nullptr, nullptr));
  assert(current_calls == before + 1);
}
}  // namespace
int main() {
  const guard::RetiredMemberGuardFunctions valid{Current, Throw, Deliver, ContainsMethod, ContainsField};
  auto incomplete = valid;
  incomplete.current_thread = nullptr;
  assert(!guard::ConfigureRetiredMemberGuard(incomplete));
  incomplete = valid; incomplete.throw_exception = nullptr;
  assert(!guard::ConfigureRetiredMemberGuard(incomplete));
  incomplete = valid; incomplete.deliver_exception = nullptr;
  assert(!guard::ConfigureRetiredMemberGuard(incomplete));
  incomplete = valid; incomplete.contains_method = nullptr;
  assert(!guard::ConfigureRetiredMemberGuard(incomplete));
  incomplete = valid; incomplete.contains_field = nullptr;
  assert(!guard::ConfigureRetiredMemberGuard(incomplete));
  assert(guard::ConfigureRetiredMemberGuard(valid));
  methods.Retire(&method);
  fields.Retire(&field);
  CheckGuard(guard::GuardRetiredMethod, &method, "Ljava/lang/NoSuchMethodError;",
             "Method removed by ART DCEVM");
  CheckGuard(guard::GuardRetiredField, &field, "Ljava/lang/NoSuchFieldError;",
             "Field removed by ART DCEVM");
  assert(throws == 4);
  expected_thread = &explicit_thread;
  if (setjmp(exception_jump) == 0) guard::DeliverRetiredMemberException(&explicit_thread);
  assert(deliveries == 1);
  assert(methods.Prune(&method, AllocatorContains) == 1);
  assert(fields.Prune(&field, AllocatorContains) == 1);
  const size_t before = throws;
  assert(!guard::GuardRetiredMethod(&method, nullptr, nullptr));
  assert(!guard::GuardRetiredField(&field, nullptr, nullptr));
  assert(throws == before);
  std::puts("retired-member guard: identity, exception, result boundary, thread lookup and pruning passed");
}
