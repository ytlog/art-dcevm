#include "platform/android13/stack_access.h"
#include "platform/common/stack_visitor_service.h"
#include <cstdlib>
namespace artdcevm::android13 {
namespace {
stack_visitor_service::Service stack_service;
static_assert(sizeof(stack_abi::Visitor)==sizeof(abi::arm64::stack::Visitor13));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#define ARTDCEVM_CHECK_CAS_VISITOR(field) \
  static_assert(offsetof(stack_abi::Visitor,field)==offsetof(abi::arm64::stack::Visitor13,field));
ARTDCEVM_CHECK_CAS_VISITOR(thread)
ARTDCEVM_CHECK_CAS_VISITOR(kind)
ARTDCEVM_CHECK_CAS_VISITOR(shadow_frame)
ARTDCEVM_CHECK_CAS_VISITOR(quick_frame)
ARTDCEVM_CHECK_CAS_VISITOR(pc)
ARTDCEVM_CHECK_CAS_VISITOR(header)
ARTDCEVM_CHECK_CAS_VISITOR(num_frames)
ARTDCEVM_CHECK_CAS_VISITOR(depth)
ARTDCEVM_CHECK_CAS_VISITOR(inline_frames)
ARTDCEVM_CHECK_CAS_VISITOR(inline_info)
ARTDCEVM_CHECK_CAS_VISITOR(stack_map)
ARTDCEVM_CHECK_CAS_VISITOR(context)
ARTDCEVM_CHECK_CAS_VISITOR(check_suspended)
#undef ARTDCEVM_CHECK_CAS_VISITOR
#pragma clang diagnostic pop
}
namespace stack_abi {
Visitor::Visitor(void* t,void* c,WalkKind k) {
  if(!stack_service.Construct(this,sizeof(*this),t,c,
      static_cast<stack_visitor_service::WalkKind>(k),true)) std::abort();
}
void Visitor::Walk(bool transitions) {
  if(!stack_service.Walk(this,sizeof(*this),transitions)) std::abort();
}
void* Visitor::Method() const {
  void* result=nullptr;
  if(!stack_service.Method(this,sizeof(*this),&result)) std::abort();
  return result;
}
}
bool ConfigureStackAccess(HookEngine& hooks,const DeviceProfile& profile,std::string* error) {
  if(!profile.Matched()) {if(error)*error="Binding requires a matched device profile";return false;}
  stack_visitor_service::Service candidate;
  if(!stack_visitor_service::Service::Bind(stack_visitor_service::Declaration::kAndroid13To16,
      [&](const char* name){return hooks.FindSymbol("libart.so",name);},&candidate)) {
    *error="Missing named stack traversal functions"; return false;
  }
  stack_service=candidate;
  return true;
}
}
