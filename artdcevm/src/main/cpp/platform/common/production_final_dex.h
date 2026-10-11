#pragma once
#include "platform/common/production_provenance.h"
#include "platform/common/redefinition_gate.h"
#include "platform/abi/common/jni_current_thread.h"

namespace artdcevm::production_bootstrap {
// One process-lifetime owner on the existing Direct dispatcher. Once published,
// this object/VM/Provenance must outlive the hook, including Install failures.
// A failed lease acquisition is retryable; missed historical public calls or an
// unrecorded success permanently invalidate the oracle rather than reuse APK.
class FinalDex final {
 public:
 explicit FinalDex(Provenance&records):records_(records),observer_{this,Begin,End}{}
 ~FinalDex(){if(pending_)std::abort();}
 FinalDex(const FinalDex&)=delete;FinalDex&operator=(const FinalDex&)=delete;
 bool Install(HookEngine&,JavaVM*,std::string*);
 private:
 static void*Begin(void*,void*,void*,void*,const void*,int32_t)noexcept;
 static void End(void*,void*,jvmtiError,bool)noexcept;
 Provenance&records_;
 const redefinition_gate::DirectObservation observer_;
 JavaVM*vm_=nullptr;abi::common::CurrentThread current_=nullptr;
 std::unique_ptr<Provenance::Reservation>pending_;
};
}
