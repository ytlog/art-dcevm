#include "platform/common/native_receiver_guard_episode.h"
#include <initializer_list>
namespace artdcevm::receiver_guard::episode {
namespace {
bool Read(Path p){return p==Path::BytecodeRead||p==Path::JniRead||p==Path::ReflectRead||p==Path::HandleRead||p==Path::VarHandle;}
bool Write(Path p){return p==Path::BytecodeWrite||p==Path::JniWrite||p==Path::ReflectWrite||p==Path::HandleWrite;}
bool Same(const Snapshot&a,const Snapshot&b){return a.body_calls==b.body_calls&&a.field_value==b.field_value&&a.sentinel==b.sentinel;}
}
Result Calibrate(Guard& guard,Source& source) noexcept {
 Result result;
 for(unsigned p=0;p<static_cast<unsigned>(Path::Count);++p)for(bool rejection:{false,true}){
  result.path=static_cast<Path>(p);result.rejection=rejection;Prepared prepared;
  if(!source.Prepare(result.path,rejection,&prepared)){result.failure=Failure::Prepare;return result;}
  const auto& call=prepared.oracle.invocation;
  if(call.path!=result.path||prepared.oracle.rejection!=rejection||call.field!=(Read(result.path)||Write(result.path))||((result.path==Path::BytecodeSpecial||result.path==Path::HandleSpecial)&&!call.special_caller)) {result.failure=Failure::Identity;return result;}
  if(!guard.BeginProof(prepared.oracle)){result.failure=Failure::Begin;return result;}
  Completion completion;Snapshot after;
  const bool ran=source.Run(result.path,&completion),observed=source.Observe(&after);
  bool oracle=ran&&observed;
  if(rejection)oracle=oracle&&!completion.succeeded&&completion.incompatible_class_change&&Same(prepared.before,after);
  else{
   oracle=oracle&&completion.succeeded&&!completion.incompatible_class_change&&after.sentinel==prepared.before.sentinel;
   if(Read(result.path))oracle=oracle&&completion.value==prepared.expected_result&&completion.value==prepared.before.field_value&&Same(prepared.before,after);
   else if(Write(result.path))oracle=oracle&&after.field_value==prepared.write_value&&after.body_calls==prepared.before.body_calls;
   else oracle=oracle&&completion.value==prepared.expected_result&&after.body_calls==prepared.before.body_calls+1&&after.field_value==prepared.before.field_value;
  }
  // Close every opened probe even on transport/readback failure. No coverage
  // is granted by the driver without the Guard's actual matching entry event.
  const bool sealed=guard.EndProof(oracle&&completion.succeeded,oracle&&completion.incompatible_class_change,oracle&&Same(prepared.before,after));
  if(!ran){result.failure=Failure::Original;return result;}
  if(!observed){result.failure=Failure::Readback;return result;}
  if(!oracle){result.failure=Failure::Oracle;return result;}
  if(!sealed){result.failure=Failure::End;return result;}
  ++result.completed;
 }
 if(!guard.Proven())result.failure=Failure::Incomplete;
 return result;
}
}
