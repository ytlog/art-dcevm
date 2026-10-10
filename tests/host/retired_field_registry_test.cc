#include "platform/common/retired_field_registry.h"

#include <cstdlib>
#include <unordered_set>

using namespace artdcevm::retirement;
namespace {
struct Allocator {
  std::unordered_set<void*> fields;
};

bool Contains(const void* allocator, void* field) {
  return static_cast<const Allocator*>(allocator)->fields.count(field) != 0;
}

void Require(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  int fields[4]{};
  RetiredFieldRegistry registry;
  Allocator first{{&fields[0], &fields[1]}};
  Allocator second{{&fields[2], &fields[3]}};

  registry.Retire(&fields[0]);
  registry.Retire(&fields[0]);
  registry.Retire(&fields[2]);
  const uint32_t first_token = registry.TokenFor(&fields[0]);
  const uint32_t second_token = registry.TokenFor(&fields[2]);
  Require(first_token != second_token && (first_token & kRetiredFieldTag) != 0);
  Require(registry.Count() == 2 && registry.SlotCount() == 2);
  Require(registry.Resolve(first_token) == &fields[0]);
  Require(registry.Resolve(1) == nullptr);
  Require(registry.Resolve(kRetiredFieldTag | 100) == nullptr);

  Require(registry.Prune(&first, Contains) == 1);
  Require(registry.TokenFor(&fields[0]) == 0 && registry.Resolve(first_token) == nullptr);
  Require(registry.Resolve(second_token) == &fields[2]);
  registry.Retire(&fields[1]);
  Require(registry.SlotCount() == 2 && registry.TokenFor(&fields[1]) == first_token);

  Require(registry.Prune(&second, Contains) == 1);
  Require(registry.Count() == 1 && registry.Resolve(second_token) == nullptr);
  Require(registry.Prune(&first, Contains) == 1 && registry.Count() == 0);
  return 0;
}
