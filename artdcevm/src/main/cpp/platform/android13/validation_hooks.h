#pragma once
#include "api/hook_engine.h"
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
// One process-wide dispatcher owns each ART hook, shared by validation probes and
// the source engine. Call initialization under the public engine's transaction lock.
class ValidationHooks {
 public:
  using Check = bool (*)(void*);
  using Commit = void (*)(void*, const void*);
  using InPlaceFinalize = void (*)();
  bool Install(HookEngine& hooks, std::string* error);
  bool installed() const;
  static void ConfigureSource(Check fields, Check methods, Check classes, Commit commit,
                              InPlaceFinalize in_place_finalize);
  static bool InSourceTransaction();
  static uint64_t FieldCalls();
  static uint64_t MethodCalls();
  static uint64_t MappingCalls();
  static uint64_t SourceCommitCalls();
  static uint64_t SourceInPlaceCalls();
};
// Only source transactions enter this scope; body/additive calls retain ART policy.
class SourceTransactionScope {
 public:
  SourceTransactionScope();
  ~SourceTransactionScope();
  SourceTransactionScope(const SourceTransactionScope&) = delete;
  SourceTransactionScope& operator=(const SourceTransactionScope&) = delete;
 private:
  bool previous_;
};
}
