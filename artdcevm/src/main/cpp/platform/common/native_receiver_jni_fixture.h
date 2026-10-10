#pragma once
#include <jni.h>
#include <string>
namespace artdcevm::receiver_guard {
// Dedicated unpublished ReceiverGuardFixture loader/Class owner. Only the
// public JNI calls execute members; this helper grants no Guard path coverage.
class JniFixtureEntries final {
 public:
 JniFixtureEntries()=default;
 ~JniFixtureEntries();
 JniFixtureEntries(const JniFixtureEntries&)=delete;
 JniFixtureEntries&operator=(const JniFixtureEntries&)=delete;
 // Ordinary Native JNI, before the fixture is published to other callers.
 // Preserve original Base method/field IDs and strong Class/loader lifetime.
 bool Register(JNIEnv*,jclass fixture,jclass base,std::string*);
 // Stop fixture callers first. An in-flight reader refuses teardown; never
 // wait under a mutex while JNI/original source callbacks are executing.
 // Failed UnregisterNatives keeps the process owner and Class roots intact.
 bool Unregister(JNIEnv*,std::string*);
 bool Registered()const noexcept;
 // Also true after incomplete registration rollback. Keep this owner alive
 // and retry Unregister; its callbacks/roots cannot be silently abandoned.
 bool Owned()const noexcept;
 private:
 struct Reader;
 static jint Method(JNIEnv*,jclass,jobject);
 static jint Read(JNIEnv*,jclass,jobject);
 static void Write(JNIEnv*,jclass,jobject,jint);
 static jint Invoke(JNIEnv*,jclass,jobject);
 enum class Phase {Idle,Installing,Registered,Retiring,Broken};
 bool Detach(JNIEnv*,std::string*);
 JavaVM*vm_=nullptr;jclass fixture_=nullptr,base_=nullptr;
 jmethodID method_=nullptr;jfieldID field_=nullptr;
 Phase phase_=Phase::Idle;size_t readers_=0;
};
}
