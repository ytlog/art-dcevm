#include "platform/common/modern_thread_deopt_service.h"
#include "literal-native-source.h"
#include <type_traits>
namespace a=artdcevm::thread_deoptimize_abi;
namespace v=artdcevm::deoptimize_if_needed_abi;
static_assert(sizeof(art::ObjPtr<art::mirror::Throwable>)==8);
static_assert(std::is_trivially_copyable_v<art::ObjPtr<art::mirror::Throwable>>);
static_assert(sizeof(art::JValue)==8&&alignof(art::JValue)==8);
static_assert(static_cast<int>(art::DeoptimizationMethodType::kKeepDexPc)==
              static_cast<int>(v::MethodType::kKeepDexPc));
static_assert(static_cast<int>(art::DeoptimizationMethodType::kDefault)==
              static_cast<int>(v::MethodType::kDefault));
static_assert(static_cast<int>(art::DeoptimizationKind::kFullFrame)==
              static_cast<int>(a::Kind::kFullFrame));
extern "C" __attribute__((noinline)) void SourcePush(
    art::Thread* thread,const art::JValue& value,bool ref,art::DeoptimizationMethodType type){
  thread->PushDeoptimizationContext(value,ref,nullptr,false,type);
}
extern "C" __attribute__((noinline)) void BridgePush(
    a::PushContext fn,void* thread,const v::JValue& value,bool ref,v::MethodType type){
  fn(thread,value,ref,{0},false,type);
}
extern "C" __attribute__((noinline)) void* SourceFullDeopt(art::Thread* thread){
  return thread->Deoptimize(art::DeoptimizationKind::kFullFrame,false,false).release();
}
extern "C" __attribute__((noinline)) void* BridgeFullDeopt(a::Deoptimize fn,void* thread){
  return fn(thread,a::Kind::kFullFrame,false,false).release();
}

extern "C" __attribute__((noinline)) bool BoundBridgeFullDeopt(
    const artdcevm::modern_thread_deopt_service::Service* service,void* runtime,
    void* thread,v::JValue value,bool is_ref,v::MethodType type){
  return service->RunFull(runtime,thread,value,is_ref,type);
}
