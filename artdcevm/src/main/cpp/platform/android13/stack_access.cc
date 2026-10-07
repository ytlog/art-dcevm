#include "platform/android13/stack_access.h"
#include <cstring>
namespace artdcevm::android13 {
namespace {
using Construct = void (*)(void*,void*,void*,stack_abi::WalkKind,bool);
Construct construct;
void (*walk)(void*,bool);
void* (*method)(const void*);
}
namespace stack_abi {
Visitor::Visitor(void* t,void* c,WalkKind k) {
  // Device constructor initializes the complete POD cache. Its base vptr is
  // restored to our declaration before the derived constructor installs its vptr.
  void* own_vptr; std::memcpy(&own_vptr,reinterpret_cast<unsigned char*>(this),sizeof(own_vptr));
  construct(this,t,c,k,true);
  std::memcpy(reinterpret_cast<unsigned char*>(this),&own_vptr,sizeof(own_vptr));
}
void Visitor::Walk(bool transitions) { walk(this,transitions); }
void* Visitor::Method() const { return method(this); }
}
bool ConfigureStackAccess(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  auto c=reinterpret_cast<Construct>(hooks.FindSymbol("libart.so","_ZN3art12StackVisitorC2EPNS_6ThreadEPNS_7ContextENS0_13StackWalkKindEb"));
  auto w=reinterpret_cast<void (*)(void*,bool)>(hooks.FindSymbol("libart.so","_ZN3art12StackVisitor9WalkStackILNS0_16CountTransitionsE0EEEvb"));
  auto m=reinterpret_cast<void* (*)(const void*)>(hooks.FindSymbol("libart.so","_ZNK3art12StackVisitor9GetMethodEv"));
  if (!c || !w || !m) { *error="Missing named stack traversal functions"; return false; }
  construct=c; walk=w; method=m;return true;
}
}
