#include "platform/symbols/art_symbol_compat.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/field_validation.h"
#include "platform/android13/method_validation.h"
#include "platform/android13/class_validation.h"
#include "platform/symbols/android13/source_symbols.h"
#include "support/elf_modules.h"
#include <atomic>
#include <vector>
namespace artdcevm::android13 {
namespace {
void* original_fields = nullptr;
void* original_methods = nullptr;
void* original_classes = nullptr;
void* original_mappings = nullptr;
void* original_commit = nullptr;
void* original_in_place = nullptr;
std::atomic<uint64_t> field_calls{0}, method_calls{0}, mapping_calls{0}, source_commit_calls{0};
std::atomic<uint64_t> source_in_place_calls{0};
bool hooks_installed = false, attempted = false;
thread_local jvmtiEnv* source_environment = nullptr;
thread_local JNIEnv* source_jni = nullptr;
ValidationHooks::Commit source_commit = nullptr;
ValidationHooks::GetJitCodeCache source_jit_code_cache = nullptr;
ValidationHooks::InvalidateJit invalidate_jit = nullptr;
bool CheckFields(void* redef) {
  ++field_calls;
  return ValidationHooks::InSourceTransaction() ? CheckEvolutionFields(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_fields)(redef);
}
bool CheckMethods(void* redef) {
  ++method_calls;
  return ValidationHooks::InSourceTransaction() ? CheckEvolutionMethods(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_methods)(redef);
}
bool CheckClass(void* redef) {
  return ValidationHooks::InSourceTransaction() ? CheckEvolutionClass(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_classes)(redef);
}
void CommitHook(void* redef, const void* iterator) {
  if (ValidationHooks::InSourceTransaction()) {
    ++source_commit_calls;
    source_commit(redef, iterator);
  } else {
    reinterpret_cast<ValidationHooks::Commit>(original_commit)(redef, iterator);
  }
}
void InPlaceHook(void* redef, const void* iterator) {
  reinterpret_cast<ValidationHooks::Commit>(original_in_place)(redef, iterator);
  if (ValidationHooks::InSourceTransaction()) {
    // ART owns the commit and its JIT suspension. Invalidate callers that may
    // still contain a previously inlined body before the transaction resumes.
    void* code_cache = source_jit_code_cache();
    if (code_cache != nullptr) invalidate_jit(code_cache);
    ++source_in_place_calls;
  }
}
void CollectMappings(void* redef, const void* iterator, void* methods, void* fields) {
  using Collect = void (*)(void*, const void*, void*, void*);
  ++mapping_calls;
  reinterpret_cast<Collect>(original_mappings)(redef, iterator, methods, fields);
}
}
bool ValidationHooks::Install(HookEngine& hooks, std::string* error) {
  if (hooks_installed) return true;
  if (attempted) { *error = "Hook initialization previously failed; restart the process"; return false; }
  attempted = true;
  if (!hooks.Initialize(error)) return false;
  if (!symbols::CheckValidationSymbols(hooks, error)) return false;
  struct Entry { const char* symbol; void* proxy; void** original; void* target = nullptr; };
  Entry entries[] = {
    {::artdcevm::symbols::android13::kCheckFields, reinterpret_cast<void*>(CheckFields), &original_fields},
    {::artdcevm::symbols::android13::kCheckMethods, reinterpret_cast<void*>(CheckMethods), &original_methods},
    {::artdcevm::symbols::android13::kCheckClass, reinterpret_cast<void*>(CheckClass), &original_classes},
    {::artdcevm::symbols::android13::kCollectMappings, reinterpret_cast<void*>(CollectMappings), &original_mappings},
    {::artdcevm::symbols::android13::kStructuralCommit, reinterpret_cast<void*>(CommitHook), &original_commit},
    {::artdcevm::symbols::android13::kInPlaceCommit, reinterpret_cast<void*>(InPlaceHook), &original_in_place},
  };
  for (auto& entry : entries) {
    entry.target = hooks.FindSymbol("libopenjdkjvmti.so", entry.symbol);
    if (!entry.target) { *error = std::string("Missing ART symbol: ") + entry.symbol; return false; }
  }
  std::vector<void*> tokens;
  for (const auto& entry : entries) {
    void* token = hooks.Install(entry.target, entry.proxy, entry.original, error);
    if (!token) {
      for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        std::string rollback;
        if (!hooks.Remove(*it, &rollback)) *error += "; unhook failed: " + rollback;
      }
      return false;
    }
    tokens.push_back(token);
  }
  hooks_installed = true;
  return true;
}
bool ValidationHooks::installed() const { return hooks_installed; }
void ValidationHooks::ConfigureSource(Commit commit,
                                      GetJitCodeCache get_jit_code_cache,
                                      InvalidateJit invalidate) {
  source_commit = commit;
  source_jit_code_cache = get_jit_code_cache;
  invalidate_jit = invalidate;
}
bool ValidationHooks::InSourceTransaction() { return source_environment != nullptr; }
jvmtiEnv* ValidationHooks::TransactionEnvironment() { return source_environment; }
JNIEnv* ValidationHooks::TransactionJni() { return source_jni; }
uint64_t ValidationHooks::FieldCalls() { return field_calls.load(); }
uint64_t ValidationHooks::MethodCalls() { return method_calls.load(); }
uint64_t ValidationHooks::MappingCalls() { return mapping_calls.load(); }
uint64_t ValidationHooks::SourceCommitCalls() { return source_commit_calls.load(); }
uint64_t ValidationHooks::SourceInPlaceCalls() { return source_in_place_calls.load(); }
SourceTransactionScope::SourceTransactionScope(jvmtiEnv* env, JNIEnv* jni)
    : previous_(source_environment), previous_jni_(source_jni) {
  source_environment = env;
  source_jni = jni;
}
SourceTransactionScope::~SourceTransactionScope() {
  source_environment = previous_;
  source_jni = previous_jni_;
}
}
