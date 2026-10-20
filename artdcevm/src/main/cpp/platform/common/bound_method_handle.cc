#include "platform/common/bound_method_handle.h"
#include "platform/common/captured_method_handle.h"
#include <atomic>
#include <cstring>
#include "evolution/modifiers.h"
#include "platform/common/art_hook.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "platform/abi/arm64/common/instrumentation.h"
#include "platform/symbols/bound_method_handle_symbols.h"

namespace artdcevm::bound_method_handle {
extern "C" {
__attribute__((visibility("hidden"))) std::atomic<void*> artdcevm_bound_entry_method{nullptr};
#if defined(__ANDROID__) && defined(__aarch64__)
void artdcevmBoundSwitchEntry(abi::arm64::SwitchContextPrefix*, const void*, const uint16_t*);
#endif
}
static_assert(offsetof(abi::arm64::SwitchContextPrefix, frame) == 16);
static_assert(offsetof(abi::arm64::ShadowFramePrefix, method) == 8);
namespace {
std::atomic<bool> ready{false}, preparing{false}, enabled{false};
std::atomic<jmethodID> transform{nullptr};
jfieldID delegate_field = nullptr, receiver_field = nullptr, kind_field = nullptr;
jfieldID type_field = nullptr, parameters_field = nullptr;
jclass handles_class = nullptr, method_class = nullptr, incompatible_class = nullptr, internal_class = nullptr;
jmethodID reflect_as = nullptr;
thread_local bool probing = false, observed = false, checking = false, native_entry = false, inspection_observed = false;
thread_local void* entering_method = nullptr;
thread_local bool forwarding_bound = false;
auto& entry_method = artdcevm_bound_entry_method;
std::atomic<bool> fast{false}, direct_entry{false};
std::atomic<uint32_t> preparation_proofs{0};
uint32_t (*frame_pc)(void*, bool) = nullptr;
int (*receiver_check)(void*, void*) = nullptr;
bool (*return_frame)(void*, bool) = nullptr;
thread_local bool capture_probe = false, public_capture = false, capture_observed = false;
thread_local JNIEnv* probe_jni = nullptr;
thread_local const std::function<bool()>* probe_frame = nullptr;
using Add = void (*)(void*, void*, uint32_t);
using AddTrace = void (*)(void*, void*, uint32_t, bool);
using Switch = void (*)(abi::arm64::SwitchContextPrefix*, const void*, const uint16_t*);
using DexPc = bool (*)(void*, const void*, const void*, uint32_t, const void*, void*);
Add original_add = nullptr;
AddTrace original_add_trace = nullptr;
Switch original_switch = nullptr;
DexPc original_pc[5]{};
void* (*this_object)(const void*, uint16_t) = nullptr;
bool (*move_exception)(void*, void*, bool, bool) = nullptr;
void* listener_hook = nullptr;
void* switch_hook = nullptr;
void* pc_hooks[5]{};
void* pc_targets[5]{};
void* instrumentation_object = nullptr;
size_t dex_pc_offset = ~size_t{0};
bool entry_hooks = false;
std::atomic<bool> inspect_entry{false}, policy_entry{false};
bool (*original_inspected)(void*, void*) = nullptr;
void* inspection_hook = nullptr;
bool registered = false;
abi::arm64::DexPcListener listener;
bool Inspected(void* callback, void* method) {
  if (inspect_entry.load(std::memory_order_acquire) && method == entry_method.load(std::memory_order_acquire)) {
    inspection_observed = true;
    return true;
  }
  return original_inspected(callback, method);
}
uint8_t EventByte(void* object, size_t offset) {
  return *(static_cast<const uint8_t*>(object) + offset);
}
void CaptureInstrumentation(void* object, uint32_t events, const std::array<uint8_t,3>& before) {
  if (events != 8 || !preparing.load(std::memory_order_acquire) || instrumentation_object) return;
  size_t selected = ~size_t{0};
  for (size_t i = 0; i < before.size(); ++i) {
    const size_t offset = abi::arm64::kDexPcListenerOffsets[i];
    if (before[i] == 0 && EventByte(object, offset) == 1) {
      if (selected != ~size_t{0}) return;
      selected = offset;
    }
  }
  if (selected != ~size_t{0}) { dex_pc_offset = selected; instrumentation_object = object; }
}
std::array<uint8_t,3> Before(void* object) {
  std::array<uint8_t,3> values{};
  for (size_t i = 0; i < values.size(); ++i) values[i] = EventByte(object, abi::arm64::kDexPcListenerOffsets[i]);
  return values;
}
void AddListener(void* object, void* value, uint32_t events) {
  const auto before = Before(object);
  original_add(object, value, events);
  CaptureInstrumentation(object, events, before);
  if (events == 8 && inspection_hook && preparing.load(std::memory_order_acquire) && !registered) {
    original_add(object, &listener, 8);
    registered = true;
  }
}
void AddTraceListener(void* object, void* value, uint32_t events, bool trace) {
  const auto before = Before(object);
  original_add_trace(object, value, events, trace);
  CaptureInstrumentation(object, events, before);
}
bool ProveFrameServices() noexcept {
  if (return_frame) return true;
  if (!probe_frame || !*probe_frame) return false;
  try { return (*probe_frame)() && return_frame; }
  catch (...) { return false; }
}
bool Target(void* frame) {
  // Identity alone publishes no metadata. Only matching frames enter the
  // acquire-protected preparation/activation paths; unrelated entries need no
  // acquire barrier (the arm64 shim uses the same relaxed predicate).
  return static_cast<const abi::arm64::ShadowFramePrefix*>(frame)->method == entry_method.load(std::memory_order_relaxed);
}
// Keep emulated TLS entirely out of the steady interpreter hook. Some Android
// toolchains otherwise hoist its resolver into unrelated interpreted calls.
__attribute__((noinline)) void ProbeSwitch(abi::arm64::SwitchContextPrefix* context,
                                         const void* implementation, const uint16_t* dex_pc) {
  if (!probing) return original_switch(context, implementation, dex_pc);
  struct Entering {
    void* previous;
    explicit Entering(void* method) : previous(entering_method) { entering_method = method; }
    ~Entering() { entering_method = previous; }
  } scope(static_cast<const abi::arm64::ShadowFramePrefix*>(context->frame)->method);
  if (Target(context->frame) && this_object(context->frame, 2)) {
    native_entry = ProveFrameServices();
    if (capture_probe) {
      capture_observed = captured_method_handle::Complete(probe_jni);
      if (capture_observed) return_frame(context->frame, true);
      else if (!move_exception(context->self, context->frame, true, true)) {
        frame_pc(context->frame, true);
        return;
      }
    }
  }
  original_switch(context, implementation, dex_pc);
}
__attribute__((noinline)) void EnterBound(abi::arm64::SwitchContextPrefix* context, const void* implementation, const uint16_t* dex_pc) {
  if (preparing.load(std::memory_order_acquire)) return ProbeSwitch(context, implementation, dex_pc);
  if (direct_entry.load(std::memory_order_acquire) && fast.load(std::memory_order_acquire) && enabled.load(std::memory_order_acquire) &&
      frame_pc && frame_pc(context->frame, false) == 0 && !return_frame(context->frame, false) && EventByte(instrumentation_object, dex_pc_offset) == 0 &&
      receiver_check) {
    const int action = receiver_check(context->self, this_object(context->frame, 2));
    if (action == 2) return_frame(context->frame, true);
    if (action == 1) {
      // Reuse ART's event-exception path. At entry there are no acquired monitors;
      // the context's result is already zero. ART chooses any local handler or
      // forced-pop state, otherwise it unwinds this frame normally.
      if (!move_exception(context->self, context->frame, true, true)) {
        frame_pc(context->frame, true);
        return;
      }
    }
  }
  original_switch(context, implementation, dex_pc);
}
// Host fallback for the fixed arm64 leaf wrapper. Keep target-only receiver
// and exception handling out of the unrelated-frame entry.
[[maybe_unused]] void Enter(abi::arm64::SwitchContextPrefix* context, const void* implementation, const uint16_t* dex_pc) {
  if (!Target(context->frame)) return original_switch(context, implementation, dex_pc);
  EnterBound(context, implementation, dex_pc);
}
template <size_t index>
__attribute__((noinline)) bool ProbePc(void* self, const void* accessor, const void* frame, uint32_t pc,
                                     const void* instrumentation, void* result) {
  if (!probing) return original_pc[index](self, accessor, frame, pc, instrumentation, result);
  void* previous = entering_method;
  entering_method = static_cast<const abi::arm64::ShadowFramePrefix*>(frame)->method;
  bool passed = original_pc[index](self, accessor, frame, pc, instrumentation, result);
  if (passed && pc == 0 && Target(const_cast<void*>(frame)) && this_object(frame, 2)) {
    native_entry = ProveFrameServices();
    if (capture_probe) {
      capture_observed = captured_method_handle::Complete(probe_jni);
      if (capture_observed) return_frame(const_cast<void*>(frame), true);
      else passed = false;
    }
  }
  entering_method = previous;
  return passed;
}
template <size_t index>
__attribute__((noinline)) bool CheckPc(void* self, const void* accessor, const void* frame, uint32_t pc,
                                     const void* instrumentation, void* result) {
  // Preserve ART's instrumentation delivery. Suppress only our own
  // Native callback work, then inspect fresh roots after all agents return to
  // Runnable. No receiver verdict or managed pointer crosses a safepoint.
  struct Forwarding {
    bool previous = forwarding_bound;
    Forwarding() { forwarding_bound = true; }
    ~Forwarding() { forwarding_bound = previous; }
  } scope;
  const bool passed = original_pc[index](self, accessor, frame, pc, instrumentation, result);
  if (!passed) return false;
  if (return_frame(const_cast<void*>(frame), false)) return true;
  const int action = receiver_check(self, this_object(frame, 2));
  if (action == 2) return_frame(const_cast<void*>(frame), true);
  return action != 1;
}
template <size_t index>
bool PcEvent(void* self, const void* accessor, const void* frame, uint32_t pc, const void* instrumentation, void* result) {
  if (preparing.load(std::memory_order_acquire)) return ProbePc<index>(self, accessor, frame, pc, instrumentation, result);
  if (pc != 0 || !Target(const_cast<void*>(frame)) || !fast.load(std::memory_order_acquire) ||
      !enabled.load(std::memory_order_acquire))
    return original_pc[index](self, accessor, frame, pc, instrumentation, result);
  return CheckPc<index>(self, accessor, frame, pc, instrumentation, result);
}
void PrepareEntry() {
  using namespace symbols::bound_method_handle;
  if (!this_object) this_object = reinterpret_cast<decltype(this_object)>(symbols::Find(kThisObject));
  if (!this_object) return;
  const symbols::NamedSymbol names[] = {kDexPc00,kDexPc01,kDexPc10,kDexPc11,kDexPcModern};
  void* replacements[] = {HookAddress(PcEvent<0>),HookAddress(PcEvent<1>),HookAddress(PcEvent<2>),HookAddress(PcEvent<3>),HookAddress(PcEvent<4>)};
  for (size_t i = 0; i < 5; ++i) if (!pc_hooks[i]) {
    pc_targets[i] = symbols::Find(names[i]);
    if (!pc_targets[i]) continue;
    // LTO/ICF can give several declared template names the same entry. Hook
    // each resolved named address once; never derive or scan for an address.
    for (size_t j = 0; j < i; ++j) if (pc_targets[i] == pc_targets[j] && pc_hooks[j]) {
      pc_hooks[i] = pc_hooks[j];
      original_pc[i] = original_pc[j];
      break;
    }
    if (!pc_hooks[i]) pc_hooks[i] = shadowhook_hook_sym_addr(pc_targets[i], replacements[i], HookSlot(&original_pc[i]));
  }
  if (!pc_hooks[4] && !(pc_hooks[0] && pc_hooks[1] && pc_hooks[2] && pc_hooks[3])) return;
  entry_hooks = true;
  // The newer active-handler interpreter can avoid global notifications.
  // Older ART can retain native PC delivery while its inspection policy lets
  // ART retire the private breakpoint and its global exit stubs.
  if (!pc_hooks[4] && !inspection_hook) if (void* entry = symbols::Find(kInspected))
    inspection_hook = shadowhook_hook_sym_addr(entry, HookAddress(Inspected), HookSlot(&original_inspected));
  if (!listener_hook) {
    if (void* entry = symbols::Find(kAddListener))
      listener_hook = shadowhook_hook_sym_addr(entry, HookAddress(AddListener), HookSlot(&original_add));
    else if (void* entry = symbols::Find(kAddTraceListener))
      listener_hook = shadowhook_hook_sym_addr(entry, HookAddress(AddTraceListener), HookSlot(&original_add_trace));
  }
  if (!pc_hooks[4]) return;
  if (!move_exception) move_exception = reinterpret_cast<decltype(move_exception)>(symbols::Find(kMoveException));
  if (!listener_hook || !move_exception) return;
  if (!switch_hook) if (void* entry = symbols::Find(kSwitch))
    switch_hook = shadowhook_hook_sym_addr(entry, HookAddress(
#if defined(__ANDROID__) && defined(__aarch64__)
      artdcevmBoundSwitchEntry
#else
      Enter
#endif
    ), HookSlot(&original_switch));

}
struct Locals {
  JNIEnv* jni;
  ~Locals() { jni->PopLocalFrame(nullptr); }
};
jclass Global(JNIEnv* jni, const char* name) {
  jclass local = jni->FindClass(name);
  return local ? static_cast<jclass>(jni->NewGlobalRef(local)) : nullptr;
}
void Failed(JNIEnv* jni) {
  if (!jni->ExceptionCheck()) jni->ThrowNew(internal_class, "Bound MethodHandle receiver inspection failed");
}
jfieldID Field(jvmtiEnv* env, jclass klass, const char* name, const char* signature) {
  jint count = 0;
  jfieldID* fields = nullptr;
  if (env->GetClassFields(klass, &count, &fields)) return nullptr;
  jfieldID result = nullptr;
  for (jint i = 0; i < count; ++i) {
    char* found_name = nullptr;
    char* found_signature = nullptr;
    if (!env->GetFieldName(klass, fields[i], &found_name, &found_signature, nullptr) &&
        std::strcmp(found_name, name) == 0 && std::strcmp(found_signature, signature) == 0) result = fields[i];
    if (found_name) env->Deallocate(reinterpret_cast<unsigned char*>(found_name));
    if (found_signature) env->Deallocate(reinterpret_cast<unsigned char*>(found_signature));
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(fields));
  return result;
}
jmethodID Transform(jvmtiEnv* env, jclass klass) {
  jint count = 0;
  jmethodID* methods = nullptr;
  if (env->GetClassMethods(klass, &count, &methods)) return nullptr;
  jmethodID result = nullptr;
  for (jint i = 0; i < count; ++i) {
    char* name = nullptr;
    char* signature = nullptr;
    if (!env->GetMethodName(methods[i], &name, &signature, nullptr) &&
        std::strcmp(name, "transform") == 0 && std::strcmp(signature, "(Ldalvik/system/EmulatedStackFrame;)V") == 0) result = methods[i];
    if (name) env->Deallocate(reinterpret_cast<unsigned char*>(name));
    if (signature) env->Deallocate(reinterpret_cast<unsigned char*>(signature));
  }
  env->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return result;
}
void Check(jvmtiEnv* env, JNIEnv* jni, jthread thread) {
  jobject bound = nullptr;
  if (env->GetLocalInstance(thread, 0, &bound) || !bound) { Failed(jni); return; }
  if (public_capture) {
    checking = false;
    capture_observed = captured_method_handle::Complete(jni, thread);
    checking = true;
    if (capture_observed && env->ForceEarlyReturnVoid(thread)) { capture_observed = false; Failed(jni); }
    return;
  }
  jobject delegate = jni->GetObjectField(bound, delegate_field);
  if (jni->ExceptionCheck()) return;
  jobject receiver = jni->GetObjectField(bound, receiver_field);
  if (jni->ExceptionCheck() || !delegate || !receiver) return;
  const jint kind = jni->GetIntField(delegate, kind_field);
  if (jni->ExceptionCheck()) return;
  jobject type = jni->GetObjectField(delegate, type_field);
  if (jni->ExceptionCheck()) return;
  auto parameters = type ? static_cast<jobjectArray>(jni->GetObjectField(type, parameters_field)) : nullptr;
  if (jni->ExceptionCheck()) return;
  if (!parameters || !jni->GetArrayLength(parameters)) { Failed(jni); return; }
  auto receiver_type = static_cast<jclass>(jni->GetObjectArrayElement(parameters, 0));
  if (!receiver_type) { Failed(jni); return; }
  if (jni->IsInstanceOf(receiver, receiver_type)) return;
  jint modifiers = 0;
  if (env->GetClassModifiers(receiver_type, &modifiers)) { Failed(jni); return; }
  if (!(modifiers & evolution::kAccInterface)) return;
  if (kind != 0 && kind != 4) {
    // Nested BindTo entries must retain their own guards while libcore runs.
    checking = false;
    const bool completed = captured_method_handle::Complete(jni, thread);
    checking = true;
    if (completed && env->ForceEarlyReturnVoid(thread)) Failed(jni);
    return;
  }
  // Public reflection encodes both pointer and index IDs. Do not decode a JNI
  // reference or read an ArtMethod while the JVMTI callback is Native.
  jobject reflected = jni->CallStaticObjectMethod(handles_class, reflect_as, method_class, delegate);
  if (jni->ExceptionCheck()) return;
  jmethodID member = reflected ? jni->FromReflectedMethod(reflected) : nullptr;
  jboolean obsolete = false;
  jclass declaring = nullptr;
  if (!member || env->IsMethodObsolete(member, &obsolete) ||
      env->GetMethodDeclaringClass(member, &declaring) || !declaring ||
      env->GetClassModifiers(declaring, &modifiers)) { Failed(jni); return; }
  if (obsolete || !(modifiers & evolution::kAccInterface)) return;
  jni->ThrowNew(incompatible_class, "MethodHandle receiver lost the declaring interface");
}
}  // namespace
extern "C" __attribute__((visibility("hidden"))) void artdcevmBoundInterpreterEntry(
    abi::arm64::SwitchContextPrefix* context, const void* implementation, const uint16_t* dex_pc) {
  EnterBound(context, implementation, dex_pc);
}

void JNICALL Breakpoint(jvmtiEnv* env, JNIEnv* jni, jthread thread,
                        jmethodID method, jlocation location) {
  if (method != transform.load(std::memory_order_acquire) || location != 0) return;
  if (forwarding_bound || capture_probe) return;
  if ((!probing && !enabled.load(std::memory_order_acquire)) || checking || jni->ExceptionCheck()) return;
  if (jni->PushLocalFrame(16)) return;
  Locals locals{jni};
  struct Checking { Checking() { checking = true; } ~Checking() { checking = false; } } scope;
  Check(env, jni, thread);
  if (probing && !jni->ExceptionCheck()) {
    observed = true;
    if (entry_hooks && entering_method) entry_method.store(entering_method, std::memory_order_release);
  }
}
EntryMode Mode() {
  return policy_entry.load(std::memory_order_acquire) ? EntryMode::kNativePolicy : !fast.load(std::memory_order_acquire) ? EntryMode::kPublicJvmti
      : direct_entry.load(std::memory_order_acquire) ? EntryMode::kNativeFrame : EntryMode::kNativeEvent;
}
uint32_t PreparationProofs() { return preparation_proofs.load(std::memory_order_acquire); }
void Enable() { enabled.store(true, std::memory_order_release); }
void Configure(uint32_t (*access)(void*, bool), int (*check)(void*, void*), bool (*finish)(void*, bool)) {
  frame_pc = access;
  receiver_check = check;
  return_frame = finish;
}
bool Prepare(JNIEnv* jni, jvmtiEnv* env, const std::function<bool()>& frame_probe) {
  if (ready.load(std::memory_order_acquire)) return true;
  bool empty = false;
  if (!preparing.compare_exchange_strong(empty, true, std::memory_order_acq_rel)) return false;
  struct Preparing { ~Preparing() { probe_frame = nullptr; probe_jni = nullptr; preparing.store(false, std::memory_order_release); } } scope;
  probe_jni = jni;
  probe_frame = &frame_probe;
  if (!jni || !env || jni->PushLocalFrame(16)) return false;
  Locals locals{jni};
  jvmtiCapabilities capabilities{};
  capabilities.can_generate_breakpoint_events = 1;
  capabilities.can_access_local_variables = 1;
  capabilities.can_force_early_return = 1;
  if (env->AddCapabilities(&capabilities)) return false;
  jmethodID entry = transform.load(std::memory_order_acquire);
  if (!entry) {
    // Publish immutable bootstrap metadata before enabling callbacks. A failed
    // breakpoint rollback may leave ART forwarding events until a retry.
    jclass bound = jni->FindClass("java/lang/invoke/Transformers$BindTo");
    if (!bound) return false;
    jclass handle = jni->FindClass("java/lang/invoke/MethodHandle");
    if (!handle) return false;
    jclass type = jni->FindClass("java/lang/invoke/MethodType");
    if (!type) return false;
    if (!handles_class) handles_class = Global(jni, "java/lang/invoke/MethodHandles");
    if (!handles_class) return false;
    if (!method_class) method_class = Global(jni, "java/lang/reflect/Method");
    if (!method_class) return false;
    if (!incompatible_class) incompatible_class = Global(jni, "java/lang/IncompatibleClassChangeError");
    if (!incompatible_class) return false;
    if (!internal_class) internal_class = Global(jni, "java/lang/InternalError");
    if (!internal_class) return false;
    delegate_field = Field(env, bound, "delegate", "Ljava/lang/invoke/MethodHandle;");
    receiver_field = Field(env, bound, "receiver", "Ljava/lang/Object;");
    kind_field = Field(env, handle, "handleKind", "I");
    type_field = Field(env, handle, "type", "Ljava/lang/invoke/MethodType;");
    parameters_field = Field(env, type, "ptypes", "[Ljava/lang/Class;");
    reflect_as = jni->GetStaticMethodID(handles_class, "reflectAs", "(Ljava/lang/Class;Ljava/lang/invoke/MethodHandle;)Ljava/lang/reflect/Member;");
    entry = Transform(env, bound);
    if (jni->ExceptionCheck() || !delegate_field || !receiver_field || !kind_field ||
        !type_field || !parameters_field || !reflect_as || !entry) return false;
    jlocation start = -1, end = -1;
    if (env->GetMethodLocation(entry, &start, &end) || start != 0 || end < 0) return false;
    transform.store(entry, std::memory_order_release);
  }
  if (!captured_method_handle::Prepare(jni, env, entry)) return false;
  PrepareEntry();
  if (env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr)) return false;
  const auto result = env->SetBreakpoint(entry, 0);
  if (result != JVMTI_ERROR_NONE && result != JVMTI_ERROR_DUPLICATE) {
    env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
    return false;
  }
  jclass probe = jni->FindClass("com/github/ytlog/artdcevm/internal/BoundMethodHandleProbe");
  jmethodID run = probe ? jni->GetStaticMethodID(probe, "run", "()V") : nullptr;
  observed = native_entry = capture_observed = false;
  probing = true;
  if (run) jni->CallStaticVoidMethod(probe, run);
  probing = false;
  bool passed = observed && !jni->ExceptionCheck();
  jint modifiers = 0;
  const bool ordinary_entry = passed && env->GetMethodModifiers(entry, &modifiers) == JVMTI_ERROR_NONE && !(modifiers & evolution::kAccSynchronized);
  preparation_proofs.store((observed ? 1 : 0) | (ordinary_entry ? 2 : 0) | (native_entry ? 4 : 0), std::memory_order_release);
  bool helper_ready = passed && entry_hooks && native_entry && return_frame && ordinary_entry;
  if (passed) {
    // The identity probe must finish normally: ForceEarlyReturn can stop ART's
    // event helper before its native-coverage postcondition. Prove public
    // continuation separately, then prove the Runnable path below.
    capture_observed = false;
    public_capture = probing = true;
    jni->CallStaticVoidMethod(probe, run);
    public_capture = probing = false;
    if (capture_observed) preparation_proofs.fetch_or(8, std::memory_order_release);
    passed = capture_observed && !jni->ExceptionCheck();
    helper_ready = helper_ready && passed;
  }
  if (helper_ready) {
    // Independently prove the Runnable JNI continuation and ART's frame-return
    // path before accepting a transaction, including the native-event fallback.
    native_entry = capture_observed = false;
    capture_probe = probing = true;
    jni->CallStaticVoidMethod(probe, run);
    capture_probe = probing = false;
    passed = !jni->ExceptionCheck();
    preparation_proofs.fetch_or((native_entry ? 16 : 0) | (capture_observed ? 32 : 0), std::memory_order_release);
    helper_ready = passed && native_entry && capture_observed;
  }
  if (helper_ready && ordinary_entry && inspection_hook && registered) {
    // The existing inspection policy keeps this bootstrap method interpreted.
    // Let ClearBreakpoint reclaim ART's global deoptimization/exit stubs.
    // Only a callback on this ClearBreakpoint caller proves the pinning
    // decision. Concurrent JIT inspection on another thread is not evidence.
    inspection_observed = false;
    inspect_entry.store(true, std::memory_order_release);
    if (env->ClearBreakpoint(entry, 0) == JVMTI_ERROR_NONE) {
      bool accelerated = false;
      if (inspection_observed &&
          env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr) == JVMTI_ERROR_NONE) {
        native_entry = capture_observed = false;
        capture_probe = true;
        probing = true;
        jni->CallStaticVoidMethod(probe, run);
        probing = capture_probe = false;
        passed = !jni->ExceptionCheck();
        accelerated = passed && native_entry && capture_observed;
      }
      policy_entry.store(accelerated, std::memory_order_release);
      if (passed && !accelerated) {
        passed = env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr) == JVMTI_ERROR_NONE;
        if (passed) {
          const auto restored = env->SetBreakpoint(entry, 0);
          passed = restored == JVMTI_ERROR_NONE || restored == JVMTI_ERROR_DUPLICATE;
        }
      }
    }
    if (!policy_entry.load(std::memory_order_acquire)) inspect_entry.store(false, std::memory_order_release);
  }
  if (helper_ready && ordinary_entry && switch_hook && instrumentation_object && entry_method.load(std::memory_order_acquire) &&
      env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr) == JVMTI_ERROR_NONE) {
    native_entry = capture_observed = false;
    capture_probe = true;
    probing = true;
    jni->CallStaticVoidMethod(probe, run);
    probing = capture_probe = false;
    // With public events off, only the already captured native identity may
    // prove continued delivery. A failed probe keeps the public guard.
    passed = !jni->ExceptionCheck();
    direct_entry.store(passed && native_entry && capture_observed, std::memory_order_release);
    if (passed && (!native_entry || !capture_observed) && env->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr)) passed = false;
  }
  if (passed) {
    preparation_proofs.fetch_or((policy_entry.load() ? 64 : 0) | (direct_entry.load() ? 128 : 0), std::memory_order_release);
    fast.store(helper_ready, std::memory_order_release);
    ready.store(true, std::memory_order_release);
  }
  else {
    fast.store(false, std::memory_order_release);
    direct_entry.store(false, std::memory_order_release);
    policy_entry.store(false, std::memory_order_release);
    inspect_entry.store(false, std::memory_order_release);
    env->ClearBreakpoint(entry, 0);
    env->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
  }
  return passed;
}
}  // namespace artdcevm::bound_method_handle
