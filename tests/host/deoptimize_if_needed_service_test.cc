#include "platform/common/deoptimize_if_needed_service.h"
#include <cassert>
#include <iostream>
using namespace artdcevm::deoptimize_if_needed_service;
namespace {
int objects[3],calls=0;void* frame=&objects[2];
abi::JValue observed{};abi::MethodType observed_type;bool observed_ref;
void Original(void* inst,void* thread,void** sp,abi::MethodType type,abi::JValue value,bool ref){
 assert(inst==&objects[0]&&thread==&objects[1]&&sp==&frame);++calls;observed=value;observed_type=type;observed_ref=ref;
}
}
int main(){
 Service service;void* symbol=reinterpret_cast<void*>(Original);
 assert(Service::Bind(Declaration::kReturnsVoid,symbol,&service));
 for(auto type:{abi::MethodType::kDefault,abi::MethodType::kKeepDexPc})for(bool ref:{false,true}){
  abi::JValue value{};value.j=INT64_C(0x1234567812345678);
  assert(service.InvokeVoid(&objects[0],&objects[1],&frame,type,value,ref));
  assert(observed.j==value.j&&observed_type==type&&observed_ref==ref);
 }
 int before=calls;abi::JValue value{};
 assert(!service.InvokeVoid(nullptr,&objects[1],&frame,abi::MethodType::kDefault,value,false));
 assert(!service.InvokeVoid(&objects[0],nullptr,&frame,abi::MethodType::kDefault,value,false));
 assert(!service.InvokeVoid(&objects[0],&objects[1],nullptr,abi::MethodType::kDefault,value,false));
 assert(!service.InvokeVoid(&objects[0],&objects[1],&frame,static_cast<abi::MethodType>(99),value,false));assert(calls==before);
 for(auto decl:{Declaration::kAbsent,Declaration::kReturnsOwnedContext,static_cast<Declaration>(99)})assert(!Service::Bind(decl,symbol,&service));
 assert(!Service::Bind(Declaration::kReturnsVoid,nullptr,&service));
 assert(!Service::Bind(Declaration::kReturnsVoid,symbol,nullptr));
 assert(service.InvokeVoid(&objects[0],&objects[1],&frame,abi::MethodType::kDefault,value,false));assert(calls==before+1);
 Service unbound;assert(!unbound.InvokeVoid(&objects[0],&objects[1],&frame,abi::MethodType::kDefault,value,false));
 std::cout<<"DeoptimizeIfNeeded: void declaration, full argument forwarding, rejected owned ABI and failed binding preservation passed\n";
}
