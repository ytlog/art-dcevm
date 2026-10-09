#pragma once
#include "api/hook_engine.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/thread_suspend_service.h"
#include <atomic>
#include <string>
namespace artdcevm::native_scope_executor {
class Executor;
// Created only by the registered ordinary native callback, after actual entry
// proof. Noncopyable and valid only during this callback's synchronous operation.
// This establishes Native JNI state, not stop-world or schema authority.
class Context final {
 public:
  Context(const Context&)=delete;
  Context& operator=(const Context&)=delete;
  void* Self()const noexcept{return self_;}
  JNIEnv* Env()const noexcept{return env_;}
  JavaVM* Vm()const noexcept{return vm_;}
  size_t Extent()const noexcept{return extent_;}
  bool Active()const noexcept;
 private:
  Context(void* self,JNIEnv* env,JavaVM* vm,size_t extent,uint64_t nonce)
      :self_(self),env_(env),vm_(vm),extent_(extent),nonce_(nonce){}
  void* self_;JNIEnv* env_;JavaVM* vm_;size_t extent_;uint64_t nonce_;
  friend class Executor;
};
// Operation runs after executor JNI preparation. It must perform no JNI or
// allocation and release every local stop/GC reservation before return or throw.
// Ordinary native modifiers do not prove an instrumentation/deopt-free exit.
// All managed inputs must already be strongly rooted; refresh after any stop.
using Operation=bool (*)(const Context&,void*);
enum class Result {Success,Unavailable,InvalidRequest,PendingException,Reentrant,
 NoCallback,ContextMismatch,StateMismatch,OperationRejected,OperationThrew,JniException};
class Executor final {
 public:
  Executor()=default;
  Executor(const Executor&)=delete;Executor&operator=(const Executor&)=delete;
  // Regular Native JNI initialization, before entering the Runnable bridge.
  // The Class global intentionally pins this registered library-owned carrier
  // for process lifetime. No managed allocation or lookup in WithNative.
  bool Prepare(HookEngine&,JNIEnv*,const thread_suspend_service::Service&,std::string*);
  // Invoke from the existing real Runnable callback; actual named current,
  // Native entry and returned Runnable state attest the supplied declaration.
  Result Calibrate(const runnable_jni::Context&);
  bool Prepared()const noexcept{return prepared_.load(std::memory_order_acquire);}
  bool Ready()const noexcept{return calibrated_.load(std::memory_order_acquire);}
  // No stop/GC reservation, borrowed Context or managed raw pointer may escape
  // this scope. A retained source reservation needs a separately proved real
  // entry/continuation/return/final-transfer owner; this executor grants none.
  Result WithNative(const runnable_jni::Context&,Operation,void*);
  // Safe pointer identity check; an expired callback address alone never grants
  // permission outside its current active invocation. Do not dereference stale
  // Contexts or retain their borrowed managed/native pointers across return.
  static bool Active(const Context*) noexcept;
 private:
  struct Request;
  inline static thread_local Request* active_=nullptr;
  static void JNICALL Dispatch(JNIEnv*,jclass,jlong) noexcept;
  Result Invoke(const runnable_jni::Context&,Operation,void*);
  bool State(void*,size_t,uint32_t)const noexcept;
  static bool Identity(const Context&,void*) noexcept;
  thread_suspend_service::Service state_;
  void* (*current_)()=nullptr;
  JavaVM* vm_=nullptr;jclass carrier_=nullptr;jmethodID method_=nullptr;
  std::atomic<bool> prepared_{false},calibrated_{false};
  std::atomic_flag preparing_=ATOMIC_FLAG_INIT;
};
}
