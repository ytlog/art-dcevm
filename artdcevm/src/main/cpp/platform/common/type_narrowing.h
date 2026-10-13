#pragma once
#include <jni.h>
#include <jvmti.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "platform/common/method_retirement.h"

// Removing a supertype narrows the type of existing instances. Before ART's
// transaction, with every other Java thread suspended until it commits, a
// public-JVMTI heap walk rejects the change if any location typed as a lost
// supertype still holds an affected instance (docs/STRUCTURAL_REDEFINE_ENGINE.md).
namespace artdcevm::type_narrowing {

// Adds the optional early signal that ART passed its class-loading pause.
void Prepare(std::vector<method_retirement::Hook>* hooks);
// After the hooks are installed: add can_suspend and can_tag_objects and gate
// threads that start while the world is suspended.
void Activate(jvmtiEnv* jvmti, std::string* note);
bool Available();

// Affected class descriptor -> descriptors of the supertypes it loses.
using LostTypes = std::map<std::string, std::set<std::string>>;

// Supertypes that `target` and its loaded subclasses lose when the target's
// direct interfaces become `after`; `removed` are the target's old direct
// interfaces that `after` drops.
jvmtiError CollectLost(JNIEnv* jni, jclass target, const std::vector<jclass>& removed,
                       const std::vector<jclass>& after, jint loaded_count,
                       const jclass* loaded, LostTypes* lost, std::string* error);

// Suspends every other Java thread and parks threads that start meanwhile;
// all of them resume when the scope ends. ART's transaction first waits for
// class definitions in progress on other threads (ScopedSuspendClassLoading);
// a thread suspended inside one would block it forever. Once armed, a watchdog
// resumes the threads if ART has not passed that pause in time, and CommitCheck
// then rejects the transaction instead of committing unprotected.
class SuspendedWorld {
 public:
  explicit SuspendedWorld(JNIEnv* jni);
  ~SuspendedWorld();
  SuspendedWorld(const SuspendedWorld&) = delete;
  SuspendedWorld& operator=(const SuspendedWorld&) = delete;
  // NONE once every other live thread is suspended.
  jvmtiError Suspend(std::string* error);
  // Starts the watchdog's deadline; call right before entering ART.
  void Arm();
  struct State;

 private:
  JNIEnv* jni_;
  State* state_;
};

// For the transaction's last failable step (after ART paused class loading
// and allocations): empty while this thread's world is still suspended,
// otherwise why the narrowing check no longer holds.
std::string CommitCheck();

// With the world suspended: describe the first location typed as a lost
// supertype (field, static field or array element) or holding without a
// static type (stack slot, JNI reference) an affected instance; empty if none.
jvmtiError FindViolation(JNIEnv* jni, const LostTypes& lost, std::string* violation);

}  // namespace artdcevm::type_narrowing
