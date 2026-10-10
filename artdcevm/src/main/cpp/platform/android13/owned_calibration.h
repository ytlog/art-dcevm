#pragma once
#include "api/hook_engine.h"
#include "platform/android13/device_profile.h"
#include "platform/abi/arm64/android13/field_copy_abi.h"
#include "platform/abi/arm64/android13/runtime_access_abi.h"
#include <jni.h>
#include <jvmti.h>
namespace artdcevm::android13 {
struct OwnedCalibrationReaders {
  void* (*method_id)(jmethodID);
  void* (*field_id)(jfieldID);
  void* (*reflected_field)(void*);
  void* (*reflected_method)(JNIEnv*,jobject);
  void* (*canonical_method)(void*);
  FieldCopyAccess fields;
};
// Bindings must already be prepared, but hooks are not needed. Matching identity
// is necessary and never substitutes for the public JNI/JVMTI observations.
// Members accepts the SDK EvolutionCalibration (value/wide/counter/compute).
// Field-copy class/field views are checked against public JNI superclass and
// JVMTI descriptor/loader/complete declared-field enumeration, independent of
// the storage checks below.
bool CalibrateOwnedMembers(const DeviceProfile&,HookEngine&,JNIEnv*,jvmtiEnv*,jobject,
                          const runtime_access_abi::Services&,const OwnedCalibrationReaders&,std::string*);
// Storage accepts the separate CalibrationFixture, never mutates its instance
// or static fields, and tests all primitive/reference/volatile combinations on
// fresh temporary instances. Static write verification requires exclusive
// bootstrap ownership of CalibrationFixture and restores every original value.
bool CalibrateOwnedFieldStorage(const DeviceProfile&,JNIEnv*,jvmtiEnv*,jobject,const OwnedCalibrationReaders&,std::string*);
// Public JNI's actual ART store observes candidate cards only while Runnable.
// Never clears live cards. Requires distinct observed-clean samples for the JNI
// oracle and owned barrier; returns false if collection/movement prevents one.
bool CalibrateOwnedCardTable(const DeviceProfile&,JNIEnv*,jobject,const OwnedCalibrationReaders&,std::string*);
// Requires prepared native transitions/SuspendAll. Real managed lock words are
// transferred only with exclusive mutator ownership, on temporary objects.
bool CalibrateOwnedMonitorTransfer(const DeviceProfile&,JNIEnv*,jobject,const FieldCopyAccess&,std::string*);
}
