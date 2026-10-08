#include "platform/common/async_deopt_service.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
using artdcevm::async_deopt_service::Declaration;
using artdcevm::async_deopt_service::Service;
struct Observation {
  const void* runtime=nullptr;
  void* method=nullptr;
  uintptr_t pc=0;
  unsigned calls=0;
  bool result=false;
} observed;
bool PcOnly(const void* runtime, uintptr_t pc) {
  observed.runtime=runtime; observed.method=nullptr; observed.pc=pc; ++observed.calls;
  return observed.result;
}
bool MethodAndPc(const void* runtime, void* method, uintptr_t pc) {
  observed.runtime=runtime; observed.method=method; observed.pc=pc; ++observed.calls;
  return observed.result;
}
void* Symbol(Declaration declaration) {
  return declaration==Declaration::kPcOnly ? reinterpret_cast<void*>(PcOnly)
                                         : reinterpret_cast<void*>(MethodAndPc);
}
void UnchangedFailure(const Service& service, const void* runtime, void* method, bool* result) {
  const unsigned calls=observed.calls;
  assert(!service.Query(runtime,method,0,result));
  assert(observed.calls==calls);
  if(result) assert(*result);
}
}

int main() {
  // The table records source coverage, not a production SDK-based selector.
  struct Family { int api; Declaration declaration; };
  constexpr Family families[]{
      {30,Declaration::kPcOnly},{31,Declaration::kPcOnly},
      {32,Declaration::kPcOnly},{33,Declaration::kPcOnly},
      {34,Declaration::kMethodAndPc},{35,Declaration::kMethodAndPc},
      {36,Declaration::kMethodAndPc},{37,Declaration::kMethodAndPc}};
  int runtime_storage=0, method_storage=0;
  const void* runtime=&runtime_storage;
  void* method=&method_storage;
  constexpr uintptr_t high_pc=UINTPTR_MAX-63;
  for(const Family& family:families) {
    assert(family.api>=30 && family.api<=37);
    const auto declaration=family.declaration;
    assert(std::strcmp(Service::SymbolName(declaration), declaration==Declaration::kPcOnly ?
        "_ZNK3art7Runtime21IsAsyncDeoptimizeableEm" :
        "_ZNK3art7Runtime21IsAsyncDeoptimizeableEPNS_9ArtMethodEm")==0);
    Service service;
    bool result=true;
    UnchangedFailure(service,runtime,method,&result);
    assert(Service::Bind(declaration,Symbol(declaration),&service));
    for(bool returned:{false,true}) {
      observed.result=returned;
      for(uintptr_t pc:{uintptr_t{0}, high_pc}) {
        const unsigned calls=observed.calls;
        result=!returned;
        assert(service.Query(runtime,method,pc,&result));
        assert(result==returned && observed.calls==calls+1);
        assert(observed.runtime==runtime && observed.pc==pc);
        assert(observed.method==(declaration==Declaration::kPcOnly ? nullptr : method));
      }
    }
    result=true;
    UnchangedFailure(service,nullptr,method,&result);
    UnchangedFailure(service,runtime,method,nullptr);
    if(declaration==Declaration::kMethodAndPc) {
      UnchangedFailure(service,runtime,nullptr,&result);
    } else {
      observed.result=false;
      assert(service.Query(runtime,nullptr,high_pc,&result));
      assert(!result && observed.pc==high_pc);
    }
    const auto invalid=static_cast<Declaration>(99);
    assert(!Service::SymbolName(invalid));
    assert(!Service::Bind(invalid,Symbol(declaration),&service));
    assert(!Service::Bind(declaration,nullptr,&service));
    assert(!Service::Bind(declaration,Symbol(declaration),nullptr));
    observed.result=true;
    result=false;
    assert(service.Query(runtime,method,high_pc,&result));
    assert(result && observed.pc==high_pc);
    assert(observed.method==(declaration==Declaration::kPcOnly ? nullptr : method));
    // Rebinding replaces the whole declaration, never leaves both callbacks.
    const auto other=declaration==Declaration::kPcOnly ? Declaration::kMethodAndPc : Declaration::kPcOnly;
    assert(Service::Bind(other,Symbol(other),&service));
    assert(service.Query(runtime,method,high_pc,&result));
    assert(observed.method==(other==Declaration::kPcOnly ? nullptr : method));
  }
  std::cout<<"async deopt service: eight API source families, typed arguments, returns and failure preservation passed\n";
}
