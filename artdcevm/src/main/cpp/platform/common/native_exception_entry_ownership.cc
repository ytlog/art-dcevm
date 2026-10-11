#include "platform/common/native_exception_entry_ownership.h"
#include "platform/common/native_source_stop.h"
#include "platform/common/modern_method_guards.h"
#include "platform/common/modern_field_guards.h"
namespace artdcevm::native_exception_entry_ownership {
std::unique_ptr<Service> Service::Prepare(const retirement::RetiredMethodRegistry&methods,
 const retirement::RetiredFieldRegistry&fields,const void*manager){
 if(!manager||!retirement::MethodEntryGuardsOwn(methods,manager)||!retirement::FieldEntryGuardsOwn(fields,manager))return nullptr;
 return std::unique_ptr<Service>(new Service(methods,fields,manager));
}
bool Service::ValidateNativeEntries(const native_source_stop::Session&stop,void*self)const noexcept {
 return stop.IsStopped(self)&&retirement::MethodEntryGuardsOwn(methods_,manager_)&&
  retirement::FieldEntryGuardsOwn(fields_,manager_)&&stop.IsStopped(self);
}
}
