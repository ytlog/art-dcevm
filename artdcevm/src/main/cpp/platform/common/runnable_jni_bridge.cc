#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::runnable_jni {
bool GlobalDecoder::Bind(Representation representation,void* symbol,GlobalDecoder* output) {
  if(representation!=Representation::kMatchedReleaseNoPoison || !symbol || !output)return false;
  GlobalDecoder candidate;candidate.decode_=reinterpret_cast<decltype(decode_)>(symbol);*output=candidate;return true;
}
bool GlobalDecoder::Decode(const Context& context,jobject strong_global,void** output) const {
  if(!decode_ || !context.self || !context.env || !context.vm || !output || context.env->ExceptionCheck())return false;
  if(!strong_global){*output=nullptr;return true;}
  if(context.env->GetObjectRefType(strong_global)!=JNIGlobalRefType ||
     context.env->ExceptionCheck())return false;
  auto decoded=decode_(context.self,strong_global);
  if(!decoded.reference || context.env->ExceptionCheck())return false;
  *output=reinterpret_cast<void*>(decoded.reference);return true;
}
}
