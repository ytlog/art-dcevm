#include "platform/android13/validation_hooks.h"
#include "platform/symbols/android13.h"
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
thread_local bool source_transaction = false;
ValidationHooks::Check source_fields = nullptr, source_methods = nullptr, source_classes = nullptr;
ValidationHooks::Commit source_commit = nullptr;
ValidationHooks::InPlaceFinalize source_in_place_finalize = nullptr;
bool CheckFields(void* redef) {
  ++field_calls;
  return source_transaction ? source_fields(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_fields)(redef);
}
bool CheckMethods(void* redef) {
  ++method_calls;
  return source_transaction ? source_methods(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_methods)(redef);
}
bool CheckClass(void* redef) {
  return source_transaction ? source_classes(redef)
      : reinterpret_cast<ValidationHooks::Check>(original_classes)(redef);
}
void CommitHook(void* redef, const void* iterator) {
  if (source_transaction) {
    ++source_commit_calls;
    source_commit(redef, iterator);
  } else {
    reinterpret_cast<ValidationHooks::Commit>(original_commit)(redef, iterator);
  }
}
void InPlaceHook(void* redef, const void* iterator) {
  reinterpret_cast<ValidationHooks::Commit>(original_in_place)(redef, iterator);
  if (source_transaction) {
    source_in_place_finalize();
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
  if (!symbols::FindProfile(33, FindLoadedModule("libart.so").build_id,
                            FindLoadedModule("libopenjdkjvmti.so").build_id)) {
    *error = "Private hooks require the exact cas ART and JVMTI build IDs";
    return false;
  }
  if (!hooks.Initialize(error)) return false;
  struct Entry { const char* symbol; void* proxy; void** original; void* target = nullptr; };
  Entry entries[] = {
    {cas::kCheckFields, reinterpret_cast<void*>(CheckFields), &original_fields},
    {cas::kCheckMethods, reinterpret_cast<void*>(CheckMethods), &original_methods},
    {cas::kCheckClass, reinterpret_cast<void*>(CheckClass), &original_classes},
    {cas::kCollectMappings, reinterpret_cast<void*>(CollectMappings), &original_mappings},
    {cas::kStructuralCommit, reinterpret_cast<void*>(CommitHook), &original_commit},
    {cas::kInPlaceCommit, reinterpret_cast<void*>(InPlaceHook), &original_in_place},
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
void ValidationHooks::ConfigureSource(Check fields, Check methods, Check classes, Commit commit,
                                      InPlaceFinalize in_place_finalize) {
  source_fields = fields; source_methods = methods; source_classes = classes; source_commit = commit;
  source_in_place_finalize = in_place_finalize;
}
bool ValidationHooks::InSourceTransaction() { return source_transaction; }
uint64_t ValidationHooks::FieldCalls() { return field_calls.load(); }
uint64_t ValidationHooks::MethodCalls() { return method_calls.load(); }
uint64_t ValidationHooks::MappingCalls() { return mapping_calls.load(); }
uint64_t ValidationHooks::SourceCommitCalls() { return source_commit_calls.load(); }
uint64_t ValidationHooks::SourceInPlaceCalls() { return source_in_place_calls.load(); }
SourceTransactionScope::SourceTransactionScope() : previous_(source_transaction) { source_transaction = true; }
SourceTransactionScope::~SourceTransactionScope() { source_transaction = previous_; }
}
