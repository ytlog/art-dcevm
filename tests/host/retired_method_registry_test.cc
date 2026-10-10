#include "platform/common/retired_method_registry.h"

#include <cstdlib>
#include <unordered_set>

using namespace artdcevm::retirement;
namespace {
struct Allocator {
  std::unordered_set<void*> methods;
};

bool Contains(const void* allocator, void* method) {
  return static_cast<const Allocator*>(allocator)->methods.count(method) != 0;
}

void Require(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main() {
  int methods[4]{};
  RetiredMethodRegistry registry;
  Allocator first{{&methods[0], &methods[1]}};
  Allocator second{{&methods[2], &methods[3]}};

  registry.Retire(&methods[0]);
  registry.Retire(&methods[0]);
  registry.Retire(&methods[2]);
  Require(registry.Count() == 2 && registry.Contains(&methods[0]));
  Require(registry.Prune(&first, Contains) == 1);
  Require(!registry.Contains(&methods[0]) && registry.Contains(&methods[2]));

  // The allocator may reuse an address after class unloading; stale identity
  // must be gone before that address represents a different method.
  registry.Retire(&methods[0]);
  Require(registry.Count() == 2);
  Require(registry.Prune(&second, Contains) == 1);
  Require(registry.Count() == 1 && registry.Contains(&methods[0]));
  Require(registry.Prune(&first, Contains) == 1 && registry.Count() == 0);
  return 0;
}
