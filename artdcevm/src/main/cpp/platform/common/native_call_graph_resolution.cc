#include "platform/common/native_call_graph_resolution.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::native_call_graph_resolution {
namespace {
bool PendingException(void*env)noexcept {
 return static_cast<JNIEnv*>(env)->ExceptionCheck()==JNI_TRUE;
}
}
Result ResolveOriginal(const runnable_jni::Context&context,receiver_guard::SourceProducer&source,
 void*caller,const receiver_guard::CodeView&staged,const std::vector<unsigned char>&bytes,
 const native_call_graph::CallSite&site){
 if(!runnable_jni::Bridge::BorrowOriginalScope(context)||!context.env)return {};
 return detail::Resolve(source.Access(),context.self,caller,staged,bytes,site,context.env,PendingException);
}
}
