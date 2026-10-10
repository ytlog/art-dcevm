#pragma once
#include "platform/common/borrowed_jni_callback.h"
#include "platform/abi/common/jni_access.h"
#include "platform/abi/arm64/android14/jni_global_decode.h"
namespace artdcevm::runnable_jni {
using borrowed_jni_callback::Context;
using borrowed_jni_callback::Operation;
using borrowed_jni_callback::Result;
using borrowed_jni_callback::Bridge;
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
