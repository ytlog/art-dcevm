#pragma once
#include <jni.h>
#include <jvmti.h>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "platform/abi/arm64/common/redefinition.h"

// State of the standard-entry transactions on this thread. A ClassFileLoadHook
// can start a nested transaction on the same thread, so contexts form a stack;
// everything one transaction keeps is pushed and popped with its context.
namespace artdcevm::transaction {

// A module's private state for one transaction, owned by the context.
struct Part {
  virtual ~Part() = default;
};

// A reference field whose declared type changes: the new field's name and
// descriptor, and its resolved type (a global reference that outlives the
// transaction). Compatible old values move to the new field.
struct RetypedType {
  std::string name;
  bool is_static;
  std::string descriptor;
  jobject type;
};

// What a target's accepted policy decision needs from the hooks on ART's
// transaction. The engine records it in CheckClass.
struct Requirements {
  // Names the target in rejections; the descriptor that verification redirects.
  std::string descriptor;
  // ART's own mapping cannot commit the change: fail the transaction, before
  // ART's no-failure point, when a later probe disables the engine's mapping.
  bool member_mapping = false;
  // Link the target's new class under this superclass (a global reference that
  // outlives the transaction), which extends the old superclass.
  jclass superclass = nullptr;
  // During ART verification, resolve the descriptor to the target's
  // replacement. Superclass changes and interface groups need it.
  bool redirect_verification = false;
  // Added interfaces (global references) that the batch may also rebuild: the
  // target links their replacements, so those rebuilds are allocated first.
  std::vector<jclass> linked_interfaces;
  std::vector<RetypedType> retyped_fields;
  // Classes of the batch on the new superclass chain, below the old superclass
  // (global references): their structural rebuilds are allocated first, and
  // the target links under the replacement of its new superclass.
  std::vector<jclass> chain_rebuilds;
};

// Native roots for JBR's affected-class set, including body-only targets and
// their loaded subtypes. Direct interface names also describe no-DEX Proxies.
struct AffectedType {
  jobject klass;
  std::vector<std::string> interfaces;
};

struct Context {
  Context* enclosing = nullptr;
  // Module state: the engine's snapshots and decisions, the member mapping
  // plans and the interface groups.
  std::unique_ptr<Part> batch;
  std::unique_ptr<Part> mapping;
  std::unique_ptr<Part> interfaces;
  std::unique_ptr<Part> dispatch;  // Pending interface dispatch obligations.
  std::unique_ptr<Part> verification;  // Current verification redirection scope.
  std::unique_ptr<Part> thread;  // Native-probed runtime thread services.
  std::unique_ptr<Part> proxies;  // Proxy classes of interface targets (proxy_relink.h).
  // Source-declared class layouts (class_layout.h) that every probe of this
  // transaction accepted, as a mask over the layout table; `probed` once any
  // probe ran.
  uint32_t layouts = ~uint32_t{0};
  bool probed = false;
  std::map<abi::arm64::ClassRedefinitionPrefix*, Requirements> targets;
  std::vector<AffectedType> affected_types;  // Global roots owned by the engine batch.
  // A rejection decided after ART allocated the new classes, taken once.
  std::string rejection;
  // ART's holder while it allocates the new classes, for finding a class's
  // replacement by identity.
  abi::arm64::RedefinitionDataHolderPrefix* holder = nullptr;

  const Requirements* Find(abi::arm64::ClassRedefinitionPrefix* self) const;
  bool RequiresMemberMapping() const;
  // One path for the engine's final checks to reject ART's whole transaction.
  bool Reject(abi::arm64::ClassRedefinitionPrefix* self, jvmtiError error, std::string reason);
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
