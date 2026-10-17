#include "platform/common/transaction_context.h"

namespace artdcevm::transaction {
namespace {
thread_local Context* current = nullptr;
}  // namespace

const Requirements* Context::Find(abi::arm64::ClassRedefinitionPrefix* self) const {
  const auto found = targets.find(self);
  return found == targets.end() ? nullptr : &found->second;
}

bool Context::RequiresMemberMapping() const {
  for (const auto& [self, requirements] : targets)
    if (requirements.member_mapping) return true;
  return false;
}

bool Context::Reject(abi::arm64::ClassRedefinitionPrefix* self, jvmtiError error, std::string reason) {
  rejection = std::move(reason);
  self->driver->result = error;
  return false;
}

Context* Current() noexcept { return current; }

Scope::Scope() noexcept {
  context_.enclosing = current;
  current = &context_;
}

Scope::~Scope() { current = context_.enclosing; }
}  // namespace artdcevm::transaction
