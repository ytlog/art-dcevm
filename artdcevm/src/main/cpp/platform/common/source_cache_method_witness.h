#pragma once
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/art_method_metadata.h"
#include <string>
namespace artdcevm::source_cache_method_witness {
// Read only the owned fixture reflection Method. The executor carrier's method
// is unrelated (or absent on the GlobalRef route) and must never be substituted.
inline bool Read(const runnable_jni::Context& context,
                 abi::arm64::android_common::FromReflectedMethod leaf,
                 jobject strong_method,const method_metadata::Layout& layout,
                 uint32_t dex_index,bool (*extent_of)(const void*,size_t*),
                 void** output,std::string* error){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!context.self||!context.env||!context.vm||!leaf||!strong_method||!extent_of||!output)
  return fail("Public cache fixture Method native reader/context unavailable");
 const abi::arm64::android_common::RunnableJniScope scope{context.self,context.env,context.vm};
 void* method=leaf(scope,strong_method);size_t extent=0;method_metadata::Identity identity;
 if(!method||!extent_of(method,&extent)||!method_metadata::ReadIdentity(layout,method,extent,&identity))
  return fail("Public cache fixture Method native declaration unreadable");
 if(identity.dex_method_index!=dex_index||(identity.access_flags&0xffffu)!=1u)
  return fail("Public cache Method differs from immutable fixture DEX");
 *output=method;return true;
}
}
