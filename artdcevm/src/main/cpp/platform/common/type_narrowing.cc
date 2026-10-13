#include "platform/common/type_narrowing.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "platform/abi/arm64/common/redefinition.h"
#include "platform/symbols/structural_redefine_symbols.h"

namespace artdcevm::type_narrowing {

struct SuspendedWorld::State {
  JavaVM* vm = nullptr;
  std::mutex mutex;
  std::condition_variable changed;
  bool armed = false;
  bool passed = false;  // ART passed its class-loading pause.
  bool finished = false;
  bool lost = false;  // The watchdog resumed the threads first.
  jthread watchdog = nullptr;  // Global reference, never suspended.
  std::vector<jthread> suspended;  // Global references, suspended by this world.
  std::thread thread;
  State* previous = nullptr;  // An enclosing world on the same thread.
};

namespace {
using Finish = bool (*)(abi::arm64::ClassRedefinitionPrefix*, void*, void*);

void* original_finish = nullptr;
jvmtiEnv* env = nullptr;
bool available = false;
thread_local SuspendedWorld::State* active_world = nullptr;
// Threads that start while a world is suspended wait before running Java code.
std::mutex gate_mutex;
std::condition_variable gate_open;
bool world_suspended = false;
// Covers ART's preparation up to its class-loading pause, and up to
// CollectAndCreateNewInstances when the early signal is not reached.
constexpr auto kWatchdogDeadline = std::chrono::seconds(3);

void JNICALL ThreadStart(jvmtiEnv*, JNIEnv*, jthread) {
  std::unique_lock<std::mutex> lock(gate_mutex);
  gate_open.wait(lock, [] { return !world_suspended; });
}

void SetGate(bool closed) {
  {
    std::lock_guard<std::mutex> lock(gate_mutex);
    world_suspended = closed;
  }
  if (!closed) gate_open.notify_all();
}

void MarkPassed() {
  SuspendedWorld::State* state = active_world;
  if (!state) return;
  std::lock_guard<std::mutex> lock(state->mutex);
  state->passed = true;
  state->changed.notify_all();
}

// The first ClassRedefinition step after ScopedSuspendClassLoading. Optional:
// an inlined call never reaches it, and CommitCheck marks the pause instead.
bool FinishNewClassAllocations(abi::arm64::ClassRedefinitionPrefix* self, void* holder,
                               void* data) {
  MarkPassed();
  return reinterpret_cast<Finish>(original_finish)(self, holder, data);
}

// Resumes what `state` suspended; the caller holds state->mutex.
void ResumeLocked(JNIEnv* jni, SuspendedWorld::State* state) {
  if (!state->suspended.empty()) {
    std::vector<jvmtiError> results(state->suspended.size());
    env->ResumeThreadList(static_cast<jint>(state->suspended.size()), state->suspended.data(),
                          results.data());
    for (jthread thread : state->suspended) jni->DeleteGlobalRef(thread);
    state->suspended.clear();
  }
  SetGate(false);
}

template <typename T> class Allocation {
 public:
  ~Allocation() { if (value) env->Deallocate(reinterpret_cast<unsigned char*>(value)); }
  T* value = nullptr;
};

// Local references owned by one scope.
class Locals {
 public:
  explicit Locals(JNIEnv* jni) : jni_(jni) {}
  ~Locals() { for (jobject ref : refs_) jni_->DeleteLocalRef(ref); }
  Locals(const Locals&) = delete;
  Locals& operator=(const Locals&) = delete;
  template <typename T> T Keep(T ref) {
    if (ref) refs_.push_back(ref);
    return ref;
  }

 private:
  JNIEnv* jni_;
  std::vector<jobject> refs_;
};

std::string Signature(jclass klass) {
  Allocation<char> signature;
  if (env->GetClassSignature(klass, &signature.value, nullptr) != JVMTI_ERROR_NONE) return {};
  return signature.value;
}

jvmtiError Interfaces(Locals* locals, jclass klass, std::vector<jclass>* output) {
  jint count = 0;
  Allocation<jclass> interfaces;
  const jvmtiError result = env->GetImplementedInterfaces(klass, &count, &interfaces.value);
  if (result != JVMTI_ERROR_NONE) return result;
  for (jint i = 0; i < count; ++i) output->push_back(locals->Keep(interfaces.value[i]));
  return JVMTI_ERROR_NONE;
}

bool AnyAssignable(JNIEnv* jni, const std::vector<jclass>& from, jclass to) {
  for (jclass klass : from)
    if (jni->IsAssignableFrom(klass, to)) return true;
  return false;
}

bool AllOthersSuspended(JNIEnv* jni, jthread watchdog) {
  Locals locals(jni);
  jthread current = nullptr;
  if (env->GetCurrentThread(&current) != JVMTI_ERROR_NONE) return false;
  locals.Keep(current);
  jint count = 0;
  Allocation<jthread> threads;
  if (env->GetAllThreads(&count, &threads.value) != JVMTI_ERROR_NONE) return false;
  bool suspended = true;
  for (jint i = 0; i < count; ++i) {
    jthread thread = locals.Keep(threads.value[i]);
    if (!suspended || jni->IsSameObject(thread, current) || jni->IsSameObject(thread, watchdog))
      continue;
    jint state = 0;
    if (env->GetThreadState(thread, &state) != JVMTI_ERROR_NONE) suspended = false;
    else if ((state & JVMTI_THREAD_STATE_ALIVE) && !(state & JVMTI_THREAD_STATE_SUSPENDED))
      suspended = false;
  }
  return suspended;
}

// Heap walk. Tagged classes carry index + 1 shifted left; bit 0 marks a
// referrer that holds an affected instance in some field.
constexpr jlong kCandidate = 1;
jlong TagOf(size_t index) { return static_cast<jlong>(index + 1) << 1; }

struct Tagged {
  std::string descriptor;
  std::set<std::string> lost;  // Non-empty: instances of this class lose these supertypes.
  bool lost_component = false;  // An array class whose elements are typed as a lost supertype.
};

struct Walk {
  const std::vector<Tagged>* tagged = nullptr;
  bool found = false;
  jvmtiHeapReferenceKind kind{};
  jmethodID method = nullptr;
  std::string where;
  size_t instance = 0;
  const Tagged* Find(jlong tag) const {
    const jlong index = (tag >> 1) - 1;
    return tag > 1 && index < static_cast<jlong>(tagged->size()) ? &(*tagged)[index] : nullptr;
  }
};

jint JNICALL Reference(jvmtiHeapReferenceKind kind, const jvmtiHeapReferenceInfo* info,
                       jlong class_tag, jlong referrer_class_tag, jlong, jlong*,
                       jlong* referrer_tag_ptr, jint, void* user_data) {
  auto* walk = static_cast<Walk*>(user_data);
  const Tagged* referee = walk->Find(class_tag);
  if (!referee || referee->lost.empty() || walk->found) return JVMTI_VISIT_OBJECTS;
  switch (kind) {
    case JVMTI_HEAP_REFERENCE_FIELD:
    case JVMTI_HEAP_REFERENCE_STATIC_FIELD:
      // The field index order is ART-internal; the field types are read later.
      if (referrer_tag_ptr) *referrer_tag_ptr |= kCandidate;
      return JVMTI_VISIT_OBJECTS;
    case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT: {
      const Tagged* array = walk->Find(referrer_class_tag);
      if (!array || !array->lost_component || !referee->lost.count(array->descriptor.substr(1)))
        return JVMTI_VISIT_OBJECTS;
      walk->where = "an element of " + array->descriptor;
      break;
    }
    case JVMTI_HEAP_REFERENCE_STACK_LOCAL:
      walk->method = info ? info->stack_local.method : nullptr;
      walk->where = "a stack frame";
      break;
    case JVMTI_HEAP_REFERENCE_JNI_LOCAL:
      walk->where = "a JNI local reference";
      break;
    case JVMTI_HEAP_REFERENCE_JNI_GLOBAL:
      walk->where = "a JNI global reference";
      break;
    default:
      return JVMTI_VISIT_OBJECTS;
  }
  walk->found = true;
  walk->kind = kind;
  walk->instance = static_cast<size_t>((class_tag >> 1) - 1);
  return JVMTI_VISIT_OBJECTS;
}

// Fields of `klass` typed as a lost supertype that hold an affected instance.
// `holder` is null for static fields.
bool CheckFields(JNIEnv* jni, jclass klass, jobject holder,
                 const std::map<std::string, size_t>& by_descriptor,
                 const std::vector<Tagged>& tagged, const std::set<std::string>& all_lost,
                 std::string* violation) {
  jint count = 0;
  Allocation<jfieldID> fields;
  if (env->GetClassFields(klass, &count, &fields.value) != JVMTI_ERROR_NONE) return false;
  for (jint i = 0; i < count; ++i) {
    jint modifiers = 0;
    if (env->GetFieldModifiers(klass, fields.value[i], &modifiers) != JVMTI_ERROR_NONE) continue;
    if (((modifiers & 0x0008) != 0) != (holder == nullptr)) continue;
    Allocation<char> name, type;
    if (env->GetFieldName(klass, fields.value[i], &name.value, &type.value, nullptr) !=
            JVMTI_ERROR_NONE || !all_lost.count(type.value))
      continue;
    Locals locals(jni);
    jobject value = locals.Keep(holder ? jni->GetObjectField(holder, fields.value[i])
                                       : jni->GetStaticObjectField(klass, fields.value[i]));
    if (jni->ExceptionCheck()) { jni->ExceptionClear(); continue; }
    if (!value) continue;
    const auto found = by_descriptor.find(Signature(locals.Keep(jni->GetObjectClass(value))));
    if (found == by_descriptor.end() || !tagged[found->second].lost.count(type.value)) continue;
    *violation = std::string(holder ? "field " : "static field ") + Signature(klass) + "." +
        name.value + " of type " + type.value + " holds an instance of " +
        tagged[found->second].descriptor;
    return true;
  }
  return false;
}
}  // namespace

void Prepare(std::vector<method_retirement::Hook>* hooks) {
  using symbols::structural::kFinishNewClassAllocations;
  if (void* finish = symbols::Find(kFinishNewClassAllocations))
    hooks->push_back({finish, reinterpret_cast<void*>(FinishNewClassAllocations),
                      &original_finish, kFinishNewClassAllocations.name});
}

void Activate(jvmtiEnv* jvmti, std::string* note) {
  jvmtiCapabilities capabilities{};
  capabilities.can_suspend = 1;
  capabilities.can_tag_objects = 1;
  if (jvmti->AddCapabilities(&capabilities) != JVMTI_ERROR_NONE) {
    *note = "interface removal unavailable: can_suspend or can_tag_objects refused";
    return;
  }
  jvmtiEventCallbacks callbacks{};
  callbacks.ThreadStart = ThreadStart;
  if (jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE ||
      jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_START, nullptr) !=
          JVMTI_ERROR_NONE) {
    *note = "interface removal unavailable: thread-start events refused";
    return;
  }
  env = jvmti;
  available = true;
}

bool Available() { return available; }

jvmtiError CollectLost(JNIEnv* jni, jclass target, const std::vector<jclass>& removed,
                       const std::vector<jclass>& after, jint loaded_count,
                       const jclass* loaded, LostTypes* lost, std::string* error) {
  Locals locals(jni);
  // The removed interfaces and their superinterfaces may each be lost.
  std::vector<jclass> candidates;
  std::set<std::string> seen;
  std::vector<jclass> pending(removed);
  while (!pending.empty()) {
    jclass next = pending.back();
    pending.pop_back();
    if (!seen.insert(Signature(next)).second) continue;
    candidates.push_back(next);
    if (Interfaces(&locals, next, &pending) != JVMTI_ERROR_NONE) {
      *error = "GetImplementedInterfaces failed";
      return JVMTI_ERROR_INTERNAL;
    }
  }
  jclass superclass = locals.Keep(jni->GetSuperclass(target));
  std::vector<jclass> lost_types;
  for (jclass type : candidates)
    if (!(superclass && jni->IsAssignableFrom(superclass, type)) && !AnyAssignable(jni, after, type))
      lost_types.push_back(type);
  if (lost_types.empty()) return JVMTI_ERROR_NONE;
  auto& target_lost = (*lost)[Signature(target)];
  for (jclass type : lost_types) target_lost.insert(Signature(type));
  // A loaded subclass keeps a type that a class between it and the target implements.
  for (jint i = 0; i < loaded_count; ++i) {
    jclass candidate = loaded[i];
    if (jni->IsSameObject(candidate, target) || !jni->IsAssignableFrom(candidate, target)) continue;
    jint status = 0;
    if (env->GetClassStatus(candidate, &status) != JVMTI_ERROR_NONE) {
      *error = "GetClassStatus failed";
      return JVMTI_ERROR_INTERNAL;
    }
    if (!(status & JVMTI_CLASS_STATUS_PREPARED)) continue;
    Locals chain(jni);
    std::vector<jclass> declared;
    for (jclass klass = candidate; klass && !jni->IsSameObject(klass, target);
         klass = chain.Keep(jni->GetSuperclass(klass))) {
      if (Interfaces(&chain, klass, &declared) != JVMTI_ERROR_NONE) {
        *error = "GetImplementedInterfaces failed";
        return JVMTI_ERROR_INTERNAL;
      }
    }
    std::set<std::string> subclass_lost;
    for (jclass type : lost_types)
      if (!AnyAssignable(jni, declared, type)) subclass_lost.insert(Signature(type));
    if (!subclass_lost.empty()) (*lost)[Signature(candidate)] = std::move(subclass_lost);
  }
  return JVMTI_ERROR_NONE;
}

SuspendedWorld::SuspendedWorld(JNIEnv* jni) : jni_(jni), state_(new State) {
  jni->GetJavaVM(&state_->vm);
}

SuspendedWorld::~SuspendedWorld() {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->finished = true;
    state_->changed.notify_all();
  }
  if (state_->thread.joinable()) state_->thread.join();
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    ResumeLocked(jni_, state_);
  }
  if (state_->watchdog) jni_->DeleteGlobalRef(state_->watchdog);
  if (active_world == state_) active_world = state_->previous;
  delete state_;
}

void SuspendedWorld::Arm() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->armed = true;
  state_->changed.notify_all();
}

namespace {
// Attached so it may resume threads; waits until armed, then until ART passes
// its class-loading pause or the deadline expires.
void Watchdog(SuspendedWorld::State* state) {
  JNIEnv* jni = nullptr;
  JavaVMAttachArgs arguments{JNI_VERSION_1_6, const_cast<char*>("artdcevm-narrowing"), nullptr};
  jthread self = nullptr;
  const bool attached = state->vm->AttachCurrentThreadAsDaemon(&jni, &arguments) == JNI_OK;
  if (attached && env->GetCurrentThread(&self) == JVMTI_ERROR_NONE) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->watchdog = static_cast<jthread>(jni->NewGlobalRef(self));
    jni->DeleteLocalRef(self);
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->watchdog) state->finished = true;  // Suspend fails without a watchdog.
    state->changed.notify_all();
    state->changed.wait(lock, [state] { return state->armed || state->finished; });
    if (!state->finished &&
        !state->changed.wait_for(lock, kWatchdogDeadline,
                                 [state] { return state->passed || state->finished; })) {
      state->lost = true;
      ResumeLocked(jni, state);
    }
  }
  if (attached) state->vm->DetachCurrentThread();
}
}  // namespace

jvmtiError SuspendedWorld::Suspend(std::string* error) {
  state_->thread = std::thread(Watchdog, state_);
  {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->changed.wait(lock, [this] { return state_->watchdog || state_->finished; });
    if (!state_->watchdog) {
      *error = "cannot attach the watchdog thread";
      return JVMTI_ERROR_INTERNAL;
    }
  }
  // Threads that were still starting become visible on a later attempt.
  constexpr int kAttempts = 50;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    SetGate(true);
    Locals locals(jni_);
    jthread current = nullptr;
    jint count = 0;
    Allocation<jthread> threads;
    if (env->GetCurrentThread(&current) != JVMTI_ERROR_NONE ||
        env->GetAllThreads(&count, &threads.value) != JVMTI_ERROR_NONE) {
      ResumeLocked(jni_, state_);
      *error = "cannot enumerate threads";
      return JVMTI_ERROR_INTERNAL;
    }
    locals.Keep(current);
    std::vector<jthread> others;
    for (jint i = 0; i < count; ++i) {
      jthread thread = locals.Keep(threads.value[i]);
      if (!jni_->IsSameObject(thread, current) && !jni_->IsSameObject(thread, state_->watchdog))
        others.push_back(thread);
    }
    std::vector<jvmtiError> results(others.size());
    if (!others.empty())
      env->SuspendThreadList(static_cast<jint>(others.size()), others.data(), results.data());
    bool failed = false;
    for (size_t i = 0; i < others.size(); ++i) {
      if (results[i] == JVMTI_ERROR_NONE)
        state_->suspended.push_back(static_cast<jthread>(jni_->NewGlobalRef(others[i])));
      else if (results[i] != JVMTI_ERROR_THREAD_SUSPENDED &&
               results[i] != JVMTI_ERROR_THREAD_NOT_ALIVE)
        failed = true;
    }
    if (failed) {
      ResumeLocked(jni_, state_);
      *error = "a thread could not be suspended";
      return JVMTI_ERROR_INTERNAL;
    }
    if (AllOthersSuspended(jni_, state_->watchdog)) {
      state_->previous = active_world;
      active_world = state_;
      return JVMTI_ERROR_NONE;
    }
    ResumeLocked(jni_, state_);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  *error = "threads kept starting";
  return JVMTI_ERROR_INTERNAL;
}

std::string CommitCheck() {
  SuspendedWorld::State* state = active_world;
  if (!state) return {};
  std::lock_guard<std::mutex> lock(state->mutex);
  state->passed = true;
  state->changed.notify_all();
  return state->lost
      ? "interface removal check expired before ART paused class loading; other threads ran again, retry"
      : std::string();
}

jvmtiError FindViolation(JNIEnv* jni, const LostTypes& lost, std::string* violation) {
  violation->clear();
  Locals locals(jni);
  jint count = 0;
  Allocation<jclass> classes;
  if (env->GetLoadedClasses(&count, &classes.value) != JVMTI_ERROR_NONE) return JVMTI_ERROR_INTERNAL;
  // Affected classes and their array classes, then arrays typed as a lost type.
  std::vector<Tagged> tagged;
  std::vector<jclass> tagged_classes;
  std::map<std::string, size_t> by_descriptor;
  std::vector<std::pair<jclass, std::string>> arrays;
  std::set<std::string> all_lost;
  for (jint i = 0; i < count; ++i) {
    jclass klass = locals.Keep(classes.value[i]);
    std::string descriptor = Signature(klass);
    const size_t dimensions = descriptor.find_first_not_of('[');
    if (dimensions == std::string::npos) continue;
    const auto found = lost.find(descriptor.substr(dimensions));
    if (dimensions > 0) arrays.emplace_back(klass, descriptor);
    if (found == lost.end()) continue;
    Tagged entry{descriptor, {}, false};
    for (const auto& type : found->second) entry.lost.insert(std::string(dimensions, '[') + type);
    all_lost.insert(entry.lost.begin(), entry.lost.end());
    by_descriptor[descriptor] = tagged.size();
    tagged.push_back(std::move(entry));
    tagged_classes.push_back(klass);
  }
  for (const auto& [klass, descriptor] : arrays) {
    if (!all_lost.count(descriptor.substr(1))) continue;
    const auto existing = by_descriptor.find(descriptor);
    if (existing != by_descriptor.end()) {
      tagged[existing->second].lost_component = true;
      continue;
    }
    by_descriptor[descriptor] = tagged.size();
    tagged.push_back({descriptor, {}, true});
    tagged_classes.push_back(klass);
  }
  for (size_t i = 0; i < tagged_classes.size(); ++i) env->SetTag(tagged_classes[i], TagOf(i));

  Walk walk;
  walk.tagged = &tagged;
  jvmtiHeapCallbacks callbacks{};
  callbacks.heap_reference_callback = Reference;
  jvmtiError result = env->FollowReferences(0, nullptr, nullptr, &callbacks, &walk);
  // Candidate referrers: untagged objects and tagged classes with bit 0 set.
  std::vector<jlong> tags{kCandidate};
  for (size_t i = 0; i < tagged.size(); ++i) tags.push_back(TagOf(i) | kCandidate);
  jint candidates = 0;
  Allocation<jobject> objects;
  Allocation<jlong> object_tags;
  if (env->GetObjectsWithTags(static_cast<jint>(tags.size()), tags.data(), &candidates,
                              &objects.value, &object_tags.value) != JVMTI_ERROR_NONE)
    result = JVMTI_ERROR_INTERNAL;
  jclass class_class = locals.Keep(jni->FindClass("java/lang/Class"));
  for (jint i = 0; i < candidates; ++i) {
    jobject object = objects.value[i];
    env->SetTag(object, object_tags.value[i] & ~kCandidate);
    if (result == JVMTI_ERROR_NONE && !walk.found && violation->empty()) {
      if (jni->IsInstanceOf(object, class_class)) {
        CheckFields(jni, static_cast<jclass>(object), nullptr, by_descriptor, tagged, all_lost,
                    violation);
      } else {
        Locals chain(jni);
        for (jclass klass = chain.Keep(jni->GetObjectClass(object)); klass && violation->empty();
             klass = chain.Keep(jni->GetSuperclass(klass)))
          CheckFields(jni, klass, object, by_descriptor, tagged, all_lost, violation);
      }
    }
    jni->DeleteLocalRef(object);
  }
  for (jclass klass : tagged_classes) env->SetTag(klass, 0);
  if (result != JVMTI_ERROR_NONE) return result;
  if (walk.found) {
    std::string where = walk.where;
    char* name = nullptr;
    jclass declaring = nullptr;
    if (walk.method && env->GetMethodName(walk.method, &name, nullptr, nullptr) == JVMTI_ERROR_NONE) {
      if (env->GetMethodDeclaringClass(walk.method, &declaring) == JVMTI_ERROR_NONE)
        where += " of " + Signature(locals.Keep(declaring)) + "." + name;
      env->Deallocate(reinterpret_cast<unsigned char*>(name));
    }
    *violation = where + " holds an instance of " + tagged[walk.instance].descriptor;
  }
  return JVMTI_ERROR_NONE;
}
}  // namespace artdcevm::type_narrowing
