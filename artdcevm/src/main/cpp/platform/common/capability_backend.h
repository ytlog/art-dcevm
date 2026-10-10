#pragma once
#include "api/runtime_backend.h"
#include "platform/common/production_bootstrap.h"
#include <functional>
#include <atomic>
namespace artdcevm {
// Public JVMTI is always attached first. A live production bootstrap is the
// only authority allowed to choose a private source candidate.
class CapabilityBackend final:public RuntimeBackend {
 public:
  using SourceFactory=std::function<std::unique_ptr<RuntimeBackend>()>;
  using Producer=std::function<std::unique_ptr<production_bootstrap::Engine>(JNIEnv*,jvmtiEnv*,jobject,std::string*)>;
  CapabilityBackend(std::unique_ptr<RuntimeBackend>,SourceFactory,Producer);
  int Attach(JavaVM*)override;
  std::string Capabilities()const override;
  int Apply(Operation,const std::vector<Definition>&)override;
  std::string Analyze(JNIEnv*,const std::vector<Definition>&)override;
  std::string ErrorName(int)const override;
  int InitializeEvolution(JNIEnv*,jobject)override;
  int InitializeCapabilities(JNIEnv*,jobject)override;
  int InitializeCommonCapabilities(JNIEnv*,jobject)override;
  int EnableValidationHooks()override;
  int EnableStandardRedefineRouting()override;
 private:
  // One nonblocking scope for every public entry, including preparation and
  // read-only reports. Never wait for a mutator-owned mutex during SuspendAll.
  class OperationScope {
   public:
    explicit OperationScope(std::atomic_flag& gate):gate_(gate),held_(!gate.test_and_set(std::memory_order_acquire)){}
    ~OperationScope(){if(held_)gate_.clear(std::memory_order_release);}
    explicit operator bool()const{return held_;}
    OperationScope(const OperationScope&)=delete;
    OperationScope& operator=(const OperationScope&)=delete;
   private:
    std::atomic_flag& gate_;bool held_;
  };
  mutable std::atomic_flag operation_=ATOMIC_FLAG_INIT;
  int Prepare(JNIEnv*,jobject);
  int PrepareSource(JNIEnv*,jobject);
  std::unique_ptr<RuntimeBackend> public_,source_;
  SourceFactory source_factory_;Producer producer_;
  std::unique_ptr<production_bootstrap::Engine> common_;
  JavaVM* vm_=nullptr;jvmtiEnv* env_=nullptr;
  bool ready_=false,source_ready_=false,attempted_=false,source_attempted_=false;
  std::string error_;
};
}
