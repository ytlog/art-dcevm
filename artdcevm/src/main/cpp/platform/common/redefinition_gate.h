#pragma once
#include "api/hook_engine.h"
#include <jvmti.h>
namespace artdcevm::redefinition_gate {
// Install at actual JVMTI ONLOAD. LIVE installation cannot prove old native
// Direct invocations have exited and deliberately cannot enable atomic graphs.
bool InstallStartup(HookEngine&,JavaVM*,jvmtiEnv*,std::string*);
// Maps only the original plugin image, never ArtPlugin_Initialize or GetEnv.
// Caller pins the handle. A mapped image does not grant readiness: subsequent
// InstallFirstLive still requires its non-reset lifetime witness after hooking.
void* MapPluginForStartup(HookEngine&,JavaVM*,std::string*);
// Before plugin initialization: named non-reset lifetime witness must still be
// null AFTER publishing Direct. Ordinary existing LIVE plugin cannot prove this.
bool InstallFirstLive(HookEngine&,JavaVM*,std::string*);
// The sole named Direct dispatcher also supplies ordinary, rejectable input
// preparation. Call in ordinary Native JNI before any installation stop. If
// the dispatcher is absent, this installer enters its own named source stop
// after taking installation serialization and resolving symbols. Registering
// preparation grants no startup/pristine-plugin readiness.
// Preparation receives borrowed arguments and returns NONE for foreign calls;
// only this dispatcher invokes original Direct, exactly once after success.
using DirectPreparation=jvmtiError(*)(void*,void*,void*,const void*,int32_t);
bool InstallDirectPreparation(HookEngine&,DirectPreparation,std::string*);
bool Ready(JavaVM*);
// Capability belongs to this actual operation thread, including its Direct body.
bool Owned(JavaVM*);
const char* Diagnostic();
class Lease {
 public:
 explicit Lease(JavaVM*);~Lease();
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 bool Acquired()const{return acquired_;}
 private:bool acquired_=false;
};
}
