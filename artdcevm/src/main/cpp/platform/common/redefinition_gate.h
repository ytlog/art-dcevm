#pragma once
#include "api/hook_engine.h"
#include <jvmti.h>
namespace artdcevm::redefinition_gate {
// Install at actual JVMTI ONLOAD. LIVE installation cannot prove old native
// Direct invocations have exited and deliberately cannot enable atomic graphs.
bool InstallStartup(HookEngine&,JavaVM*,jvmtiEnv*,std::string*);
// Before plugin initialization: named non-reset lifetime witness must still be
// null AFTER publishing Direct. Ordinary existing LIVE plugin cannot prove this.
bool InstallFirstLive(HookEngine&,JavaVM*,std::string*);
bool Ready(JavaVM*);
const char* Diagnostic();
class Lease {
 public:
 explicit Lease(JavaVM*);~Lease();
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 bool Acquired()const{return acquired_;}
 private:bool acquired_=false;
};
}
