#pragma once
#include "platform/common/retired_method_registry.h"
#include "platform/common/retired_field_registry.h"
#include <memory>
namespace artdcevm::native_source_stop {class Session;}
namespace artdcevm::native_exception_entry_ownership {
class Service;
// This covers the installed native JNI member entry contract only. It does not
// replace same-Plan roots, continuation/callee proofs or original upcall facts.
class Proof {
 public:
 virtual ~Proof()=default;
 Proof(const Proof&)=delete;Proof&operator=(const Proof&)=delete;
 Proof(Proof&&)=delete;Proof&operator=(Proof&&)=delete;
 virtual bool ValidateNativeEntries(const native_source_stop::Session&,void*self)const noexcept=0;
 private:
 friend class Service;
 Proof()=default;
};
class Service final:public Proof {
 public:
 static std::unique_ptr<Service> Prepare(const retirement::RetiredMethodRegistry&,
   const retirement::RetiredFieldRegistry&,const void*observed_jni_id_manager);
 bool ValidateNativeEntries(const native_source_stop::Session&,void*self)const noexcept override;
 private:
 Service(const retirement::RetiredMethodRegistry&methods,const retirement::RetiredFieldRegistry&fields,const void*manager)
  :methods_(methods),fields_(fields),manager_(manager){}
 const retirement::RetiredMethodRegistry&methods_;const retirement::RetiredFieldRegistry&fields_;
 const void*manager_;
};
}
