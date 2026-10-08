#pragma once
#include "platform/abi/arm64/android11/jni_access.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
namespace artdcevm::runnable_jni {
struct Context {void* self;JNIEnv* env;void* vm;};
using Operation=bool (*)(const Context&,void*);
enum class Result {kSuccess,kUnavailable,kInvalidRequest,kPendingException,kReentrant,
                   kLeafNotReached,kContextMismatch,kOriginalRejected,kOperationRejected,
                   kOperationThrew,kJniException};
// Caller owns matching/calibration and hook installation. Bind with the original
// trampoline, then dispatch the named leaf through OnFromReflectedMethod. Keep
// service, hook and trampoline alive until all invocations and passthroughs end;
// never rebind or remove a hook while they are in flight. No hook is installed here.
class Bridge {
 public:
  using Leaf=abi::arm64::android11::FromReflectedMethod;
  static const char* SymbolName(){return abi::arm64::android11::kFromReflectedMethod;}
  static bool Bind(void* original,Bridge* output);
  // carrier must be a live owned Method/Constructor usable by this JNIEnv.
  // ART's real JNI function establishes Runnable+mutator scope. Callback runs
  // within its named leaf; if LTO bypasses the hook this returns kLeafNotReached.
  // No managed pointer may escape callback without an ART/JNI root. Callback must
  // preserve carrier and native hook lifetimes. Pending exceptions are not cleared.
  Result Invoke(JNIEnv* env,jobject carrier,Operation operation,void* data) const;
  void* OnFromReflectedMethod(const abi::arm64::android11::RunnableJniScope& scope,
                             jobject carrier) const;
 private:
  struct Request;
  static thread_local Request* active_;
  Leaf original_=nullptr;
};
class GlobalDecoder {
 public:
  enum class Representation {kMatchedReleaseNoPoison};
  static const char* SymbolName(){return abi::arm64::android14::kDecodeGlobalJObject;}
  static bool Bind(Representation representation,void* symbol,GlobalDecoder* output);
  // Use only inside an independently established live Runnable JNI Context.
  // Accepts strong JNI globals exclusively. Caller creates/deletes them with JNI
  // and keeps them live; local/weak refs must never be passed to the native leaf.
  // Result is borrowed only until suspension/GC; root before a safepoint.
  bool Decode(const Context& context,jobject strong_global,void** output) const;
 private:
  abi::arm64::android14::DecodeGlobalJObject decode_=nullptr;
};
}
