#pragma once
#include <jni.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"

namespace artdcevm::verification_redirect {
struct Verification;
}

// State of the standard-entry transactions on this thread. A ClassFileLoadHook
// can start a nested transaction on the same thread, so contexts form a stack;
// everything one transaction keeps is pushed and popped with its context.
namespace artdcevm::transaction {

// A module's private state for one transaction, owned by the context.
struct Part {
  virtual ~Part() = default;
};

// What a target's accepted policy decision needs from the hooks on ART's
// transaction. The engine records it in CheckClass.
struct Requirements {
  // Names the target in rejections; the descriptor that verification redirects.
  std::string descriptor;
  // ART's own mapping cannot commit the change: fail the transaction, before
  // ART's no-failure point, when a later probe disables the engine's mapping.
  bool member_mapping = false;
  // The target becomes abstract: fail the transaction, before ART's
  // no-failure point, if instances of exactly that class exist.
  bool no_instances = false;
  // Link the target's new class under this superclass (a global reference that
  // outlives the transaction), which extends the old superclass.
  jclass superclass = nullptr;
  // During ART verification, resolve the descriptor to the target's
  // replacement. Superclass changes and interface groups need it.
  bool redirect_verification = false;
};

// Batch-wide: an input interface may be a dependent of another group owner.
struct InterfaceObligation {
  abi::arm64::ClassRedefinitionPrefix* interface;
  std::string name;
  std::string descriptor;
};

struct Context {
  Context* enclosing = nullptr;
  // Module state: the engine's snapshots and decisions, the member mapping
  // plans and the interface groups.
  std::unique_ptr<Part> batch;
  std::unique_ptr<Part> mapping;
  std::unique_ptr<Part> interfaces;
  // Source-declared class layouts (class_layout.h) that every probe of this
  // transaction accepted, as a mask over the layout table; `probed` once any
  // probe ran.
  uint32_t layouts = ~uint32_t{0};
  bool probed = false;
  std::map<abi::arm64::ClassRedefinitionPrefix*, Requirements> targets;
  std::vector<InterfaceObligation> interface_obligations;
  // Set once a subclass of a target is loaded after the engine's snapshot:
  // neither ART's recreated subclasses nor the policy decision include it.
  const std::atomic<bool>* subclass_loaded = nullptr;
  // A rejection decided after ART allocated the new classes, taken once.
  std::string rejection;
  // The target ART is verifying, while verification is redirected.
  const verification_redirect::Verification* verifying = nullptr;

  const Requirements* Find(abi::arm64::ClassRedefinitionPrefix* self) const;
  bool RequiresMemberMapping() const;
};

// The innermost transaction on this thread, or null outside one.
Context* Current() noexcept;

// Pushes a new context for the lifetime of the scope.
class Scope {
 public:
  Scope() noexcept;
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  Context& context() { return context_; }

 private:
  Context context_;
};

// The part `T` that `context` holds in `slot`, or null.
template <typename T> T* PartOf(const Context* context, std::unique_ptr<Part> Context::*slot) {
  return context ? static_cast<T*>((context->*slot).get()) : nullptr;
}
}  // namespace artdcevm::transaction
