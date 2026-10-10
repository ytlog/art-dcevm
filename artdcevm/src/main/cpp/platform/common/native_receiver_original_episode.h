#pragma once
#include "platform/common/native_receiver_jni_fixture.h"
namespace artdcevm::receiver_guard {
struct OriginalEntriesResult {unsigned completed=0;bool passed=false;};
// Execute the sixteen real original Java/JNI entries against one independently
// loaded, rooted fixture. This is the success half only: no Guard probe, ICCE,
// migrated receiver, permanent coverage or permission is manufactured here.
// Keep owner alive on failed unregister/pending original exception. Caller
// describes that exception, then retries owner.Unregister after handling it.
OriginalEntriesResult RunOriginalReceiverEntries(JNIEnv*,jobject fixture,
                                                JniFixtureEntries&retained_owner,
                                                std::string*error);
}
