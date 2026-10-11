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
 // Installation callers serialize; the published VM/leaf stay immutable.
 bool Install(HookEngine&,JavaVM*,std::string*);
 Provenance*Borrow(JavaVM*)const noexcept;
 private:
 static void*Begin(void*,void*,void*,void*,const void*,int32_t)noexcept;
 static void End(void*,void*,jvmtiError,bool)noexcept;
 Provenance&records_;
 const redefinition_gate::DirectObservation observer_;
 JavaVM*vm_=nullptr;abi::common::CurrentThread current_=nullptr;
 std::unique_ptr<Provenance::Reservation>pending_;
};
// Install immediately after the actual startup/pristine Direct gate, before
// plugin/environment attachment or public calls. The single process owner
// lives as long as the retained hook; engine destruction cannot release it.
bool InstallFinalDexAtStartup(HookEngine&,JavaVM*,std::string*);
// No allocation, lock or JNI. Only the actual owned Direct lease can borrow
// the fully observed source; all accesses end before that lease is released.
Provenance*BorrowFinalDexRecords(JavaVM*)noexcept;
}
