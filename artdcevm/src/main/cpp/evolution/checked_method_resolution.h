#pragma once
#include <cstdint>

namespace artdcevm::evolution {
// Explicit ART handles stay owned/rooted by the caller. A provider must not
// substitute the referrer's DexCache/loader; referrer may be null.
struct MethodResolutionContext {
  void* linker;
  uint32_t method_index;
  void* dex_cache_handle;
  void* class_loader_handle;
  void* referrer;
  uint32_t invoke_type;
};

// Implementations own the rooted referenced class and reacquire its managed
// pointer after every suspension-capable operation. All primitives must be
// bound and calibrated before a provider can be used by an invocation Hook.
// The caller holds ART's shared mutator lock, as in ClassLinker::ResolveMethod.
class CheckedMethodResolution {
 public:
  virtual ~CheckedMethodResolution() = default;
  virtual void* CachedMethod(const MethodResolutionContext&) = 0;
  // Failure installs the class-resolution exception.
  virtual bool ResolveAndRootClass(const MethodResolutionContext&) = 0;
  // Rejection installs the class/invoke mismatch exception.
  virtual bool RejectClassInvokeType(const MethodResolutionContext&) = 0;
  virtual void* FindResolvedMethod(const MethodResolutionContext&) = 0;
  virtual bool RejectAccess(const MethodResolutionContext&, void* method) = 0;
  // Returns mismatch only; ThrowInvokeMismatch installs the exception later.
  virtual bool RejectMethodInvokeType(const MethodResolutionContext&, void* method) = 0;
  virtual void* FindIncompatibleMethod(const MethodResolutionContext&) = 0;
  virtual void ThrowInvokeMismatch(const MethodResolutionContext&, void* method) = 0;
  virtual void ThrowMissingMethod(const MethodResolutionContext&) = 0;
};

// Source-defined checked Handle ResolveMethod ordering: capture the cache
// before resolving the referenced class. A null result means
// the provider installed ART's exception. This is not an unchecked resolver.
void* ResolveCheckedMethod(const MethodResolutionContext&, CheckedMethodResolution&);

// android-16/17 ClassLinker::ResolveMethodWithChecks ordering: resolve/check
// the referenced class before reading the method cache (resolution may fill it).
// The version provider supplies the normalized non-null referrer, its own
// cache/loader context, and access checks against the already rooted class.
// Proxy constructors must be normalized by that provider before this call.
// This is the checked operation, never the unchecked ResolveMethodId fast path.
void* ResolveCheckedReferrerMethod(const MethodResolutionContext&, CheckedMethodResolution&);
}
