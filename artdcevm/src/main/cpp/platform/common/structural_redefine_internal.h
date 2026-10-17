#pragma once
#include <set>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "platform/common/structural_redefine_engine.h"
#include "platform/common/class_layout.h"
#include "platform/common/hierarchy_snapshot.h"
#include "platform/common/interface_redefinition.h"
#include "platform/common/structural_policy.h"
#include "platform/common/transaction_context.h"
namespace artdcevm::structural_redefine::detail {
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::RedefinitionDataIter;
using Entry = jvmtiError (*)(jvmtiEnv*, jint, const jvmtiClassDefinition*);
using Check = bool (*)(ClassRedefinitionPrefix*);
using Record = void (*)(ClassRedefinitionPrefix*);

struct ProcessCapabilities {
  bool runtime_thread = false;
  bool member_mapping = false;
  bool retirement = false;
  bool field_retirement = false;
  bool superclass_change = false;
  bool instance_check = false;
  bool subclass_watch = false;
};
struct RetiredKinds {
  jobject klass;
  structural_policy::RetiredMethods methods;
};
struct Watch;
struct Process {
  JavaVM* runtime_vm = nullptr;
  jvmtiEnv* snapshot_env = nullptr;
  Entry structural_entry = nullptr;
  Record record_field = nullptr;
  Record record_method = nullptr;
  void* original_entry = nullptr;
  void* original_class = nullptr;
  void* original_fields = nullptr;
  void* original_methods = nullptr;
  void* original_instances = nullptr;
  void* original_allocate = nullptr;
  class_layout::Services layout_services{};
  // Filled only by Install. Per-transaction probes further restrict these abilities.
  ProcessCapabilities capabilities;
  std::vector<void*> hook_tokens;
  // ART takes its own redefinition lock after our Native-state snapshots.
  // Keep snapshots and the transaction together so another routed batch cannot
  // validate against an older definition while waiting for ART's lock.
  std::recursive_mutex transaction_mutex;
  std::mutex report_mutex;
  bool installed = false;
  std::string diagnostic = "engine not installed";
  std::string last_rejection;

  std::vector<RetiredKinds> retired_kinds;
  std::mutex watch_mutex;
  std::vector<Watch*> watches;
  std::atomic<uint64_t> subclass_retries{0};

  // Published by Install; hooks read it without the transaction lock.
  std::atomic<interface_redefinition::Backend*> interface_backend{nullptr};
  std::string interface_runtime = "unavailable";
};
extern std::mutex installation_mutex;
extern std::unique_ptr<Process> process;
// The published interface backend, or null. Load once per use site.
inline interface_redefinition::Backend* InterfaceBackend() {
  return process->interface_backend.load(std::memory_order_acquire);
}
struct Snapshot : hierarchy_snapshot::Snapshot {
  // Methods retired earlier, and declarations this transaction retires.
  structural_policy::RetiredMethods retired;
  std::vector<evolution::Member> retiring;
};

// The engine's part of a transaction context.
struct Batch final : transaction::Part {
  std::map<jclass, Snapshot> classes;
  std::map<ClassRedefinitionPrefix*, structural_policy::Decision> decisions;
  JNIEnv* jni = nullptr;
  std::vector<jobject> retirement_roots;
  std::vector<jobject> type_roots;
  bool retires_members = false;
  std::set<ClassRedefinitionPrefix*> no_instances;
  const std::atomic<bool>* subclass_loaded = nullptr;
  ~Batch() override {
    if (!jni) return;
    for (jobject root : retirement_roots) jni->DeleteGlobalRef(root);
    for (jobject root : type_roots) jni->DeleteGlobalRef(root);
  }
};
inline Batch* BatchOf(const transaction::Context* context) {
  return transaction::PartOf<Batch>(context, &transaction::Context::batch);
}

void Diagnose(const std::string&, bool rejection = false);
void JNICALL ClassPrepared(jvmtiEnv*, JNIEnv*, jthread, jclass) noexcept;
jvmtiError Route(jvmtiEnv* env, jint count, const jvmtiClassDefinition* definitions) noexcept;
bool CheckClass(ClassRedefinitionPrefix* self) noexcept;
bool CheckFields(ClassRedefinitionPrefix* self) noexcept;
bool CheckMethods(ClassRedefinitionPrefix* self) noexcept;
void* AllocateNewClassObject(ClassRedefinitionPrefix* self, abi::arm64::Handle old_class, abi::arm64::Handle super_class,
                             abi::arm64::Handle cache, uint16_t index) noexcept;
bool CollectAndCreateNewInstances(ClassRedefinitionPrefix* self, RedefinitionDataIter* data) noexcept;
}  // namespace artdcevm::structural_redefine::detail
