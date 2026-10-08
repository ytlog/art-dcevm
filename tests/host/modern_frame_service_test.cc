#include "platform/common/modern_frame_service.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
using artdcevm::modern_frame::Model;
using artdcevm::modern_frame::Service;
struct Call {void* instrumentation=nullptr;void* thread=nullptr;bool force=false;unsigned count=0;} call;
void Instrument(void* instrumentation,void* thread,bool force) {
  call.instrumentation=instrumentation;call.thread=thread;call.force=force;++call.count;
}
void InstrumentLegacy(void* instrumentation,void* thread) {
  call.instrumentation=instrumentation;call.thread=thread;call.force=false;++call.count;
}
void* Symbol(Model model) {
  return model==Model::kLegacyNoForceExitStack ? reinterpret_cast<void*>(InstrumentLegacy)
                                            : reinterpret_cast<void*>(Instrument);
}
void AssertCall(const Service& service,Model model,void* instrumentation,void* thread) {
  unsigned before=call.count;
  if(model!=Model::kFrameDeoptFlags) {
    assert(service.PrepareLegacyStack(instrumentation,thread));
    assert(!call.force);
    assert(!service.PrepareCheckOnly(instrumentation,thread));
    assert(!service.ForceThreadFrames(instrumentation,thread));
    assert(call.count==before+1);
  } else {
    assert(!service.PrepareLegacyStack(instrumentation,thread));
    assert(call.count==before);
    assert(service.PrepareCheckOnly(instrumentation,thread));
    assert(!call.force && call.count==before+1);
    assert(service.ForceThreadFrames(instrumentation,thread));
    assert(call.force && call.count==before+2);
  }
  assert(call.instrumentation==instrumentation && call.thread==thread);
}
void AssertNoCalls(const Service& service,void* instrumentation,void* thread) {
  unsigned before=call.count;
  assert(!service.PrepareLegacyStack(instrumentation,thread));
  assert(!service.PrepareCheckOnly(instrumentation,thread));
  assert(!service.ForceThreadFrames(instrumentation,thread));
  assert(call.count==before);
}
}
int main() {
  assert(std::strcmp(Service::SymbolName(Model::kLegacyExitStack),
      "_ZN3art15instrumentation15Instrumentation21InstrumentThreadStackEPNS_6ThreadEb")==0);
  assert(std::strcmp(Service::SymbolName(Model::kFrameDeoptFlags),
      "_ZN3art15instrumentation15Instrumentation21InstrumentThreadStackEPNS_6ThreadEb")==0);
  assert(std::strcmp(Service::SymbolName(Model::kLegacyNoForceExitStack),
      "_ZN3art15instrumentation15Instrumentation21InstrumentThreadStackEPNS_6ThreadE")==0);
  assert(!Service::SymbolName(static_cast<Model>(99)));
  int instrumentation_storage=0,thread_storage=0;
  void* instrumentation=&instrumentation_storage;
  void* thread=&thread_storage;
  Service unbound;
  AssertNoCalls(unbound,instrumentation,thread);
  struct SourceFamily {int api;Model model;};
  // Documentation of fixed source coverage, not a production SDK selector.
  constexpr SourceFamily families[]{
      {30,Model::kLegacyNoForceExitStack},{31,Model::kLegacyNoForceExitStack},
      {32,Model::kLegacyNoForceExitStack},{33,Model::kLegacyExitStack},
      {34,Model::kFrameDeoptFlags},{35,Model::kFrameDeoptFlags},
      {36,Model::kFrameDeoptFlags},{37,Model::kFrameDeoptFlags}};
  for(const auto& family:families) {
    assert(family.api>=30 && family.api<=37);
    Service service;
    assert(Service::Bind(family.model,Symbol(family.model),&service));
    AssertCall(service,family.model,instrumentation,thread);
    AssertNoCalls(service,nullptr,thread);
    AssertNoCalls(service,instrumentation,nullptr);
    assert(!Service::Bind(family.model,nullptr,&service));
    assert(!Service::Bind(static_cast<Model>(99),Symbol(family.model),&service));
    assert(!Service::Bind(family.model,Symbol(family.model),nullptr));
    AssertCall(service,family.model,instrumentation,thread);
    const auto other=family.model!=Model::kFrameDeoptFlags ? Model::kFrameDeoptFlags : Model::kLegacyNoForceExitStack;
    assert(Service::Bind(other,Symbol(other),&service));
    AssertCall(service,other,instrumentation,thread);
  }
  std::cout<<"modern frame service: named ABI, explicit models, force argument, rejected calls and binding preservation passed\n";
}
