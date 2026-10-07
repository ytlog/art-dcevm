#pragma once
#include "api/hook_engine.h"
#include <jni.h>
#include <string>
namespace artdcevm::android13 {
bool ConfigureMemberAccessGuards(HookEngine&,void* implementation,JNIEnv*,jobject,std::string*);
bool ReceiverGuardEnabled();
bool ReceiverGuardRequired();
bool GuardNativeFieldReceiver(JNIEnv*,jobject,jfieldID);
bool GuardRunnableFieldReceiver(const void*,jobject,jfieldID);
bool GuardRunnableMethodReceiver(const void*,jobject,void*);
bool GuardRunnableMethodIdReceiver(const void*,jobject,jmethodID);
bool GuardMethodHandleTarget(void* thread,void* slot,void* result);
bool GuardFieldVarHandle(void* handle,int mode,void* frame,const void* operands,void* result);
bool GuardMethodHandleFieldPut(void* field);
bool GuardNterpFinalFieldPut(void* thread,void* caller,uint16_t* dex_pc);
}
