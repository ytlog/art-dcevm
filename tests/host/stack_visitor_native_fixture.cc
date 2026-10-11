#include "stack_visitor_native_fixture.h"
#include <cstdlib>
#include <cstring>

namespace stack_fixture {
Observation observed;
using namespace artdcevm;
template<class Storage> struct ForeignVisitor final:Storage {
  bool VisitFrame() override {std::abort();}
};
template<class Storage> void Construct(void* raw,void* thread,void* context,
                                    stack_visitor_service::WalkKind kind,bool check) {
  ++observed.constructs;
  observed.thread=thread;observed.context=context;observed.kind=kind;observed.check=check;
  auto* storage=static_cast<Storage*>(raw);
  storage->thread=thread;storage->context=context;storage->kind=kind;
  storage->shadow_frame=nullptr;storage->quick_frame=nullptr;storage->pc=0;
  storage->header=nullptr;storage->num_frames=0;storage->depth=0;
  storage->inline_frames={};storage->inline_info={};storage->stack_map={};
  storage->check_suspended=check;
  // A separate native component overwrites the vptr, as ART's base C2 does.
  // Its VisitFrame aborts, so successful callbacks prove caller vptr restoration.
  ForeignVisitor<Storage> foreign;
  void* foreign_vptr;
  std::memcpy(&foreign_vptr,static_cast<const void*>(&foreign),sizeof(foreign_vptr));
  std::memcpy(raw,&foreign_vptr,sizeof(foreign_vptr));
}
template<class Storage> __attribute__((noinline)) bool VirtualCall(Storage* storage) {
  return storage->VisitFrame();
}
template<class Storage> void Walk(void* raw,bool transitions) {
  ++observed.walks;observed.transitions=transitions;
  auto* storage=static_cast<Storage*>(raw);
  for(size_t depth=0;depth<3;++depth) {
    storage->depth=depth;
    if(!VirtualCall(storage)) break;
  }
}
template<class Storage> void* Method(const void* raw) {
  ++observed.methods;
  auto* storage=static_cast<const Storage*>(raw);
  return storage->quick_frame ? *storage->quick_frame : nullptr;
}
template<class Storage> uint32_t DexPc(const void*raw,bool abort){if(abort)std::abort();return uint32_t(static_cast<const Storage*>(raw)->pc);}
template<class Storage> void* Find(const char* name,const char* walk_name) {
  using Service=stack_visitor_service::Service;
  if(std::strcmp(name,Service::ConstructSymbol())==0) return reinterpret_cast<void*>(Construct<Storage>);
  if(std::strcmp(name,walk_name)==0) return reinterpret_cast<void*>(Walk<Storage>);
  if(std::strcmp(name,Service::DexPcSymbol())==0)return reinterpret_cast<void*>(DexPc<Storage>);
  if(std::strcmp(name,Service::MethodSymbol())==0) return reinterpret_cast<void*>(Method<Storage>);
  return nullptr;
}
void* Symbol(stack_visitor_service::Declaration declaration,const char* name) {
  using namespace stack_visitor_service;
  switch(declaration) {
    case Declaration::kAndroid11: return Find<abi::arm64::stack::Visitor11>(name,Service::WalkSymbol(declaration));
    case Declaration::kAndroid12: return Find<abi::arm64::stack::Visitor12>(name,Service::WalkSymbol(declaration));
    case Declaration::kAndroid13To16: return Find<abi::arm64::stack::Visitor13>(name,Service::WalkSymbol(declaration));
    case Declaration::kAndroid17: return Find<abi::arm64::stack::Visitor17>(name,Service::WalkSymbol(declaration));
  }
  return nullptr;
}
}
