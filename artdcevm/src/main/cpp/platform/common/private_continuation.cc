#include "platform/common/private_continuation.h"
#include "platform/common/interpreter_events.h"
#include "platform/common/interpreter_notifications.h"
#include "platform/common/continuation_profile.h"
#include "platform/common/runtime_roots.h"
#include "platform/common/fault_injection.h"
#include "platform/abi/arm64/android13/continuation.h"
#include "platform/abi/arm64/shadow_frame_table.h"
#include "platform/symbols/private_continuation_symbols.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include "evolution/modifiers.h"
#include <android/api-level.h>
#include <atomic>
#include <cstring>
#include <vector>

namespace artdcevm::private_continuation {
std::atomic<bool> pc_active{false};
namespace {
using Frame = abi::arm64::android13::ShadowFrame;
using Accessor = abi::arm64::android13::CodeItemDataAccessor;
using Call = bool (*)(void*, void*, void*, const uint16_t*, uint16_t, uint64_t*);
Services services{};
Call calls[4]{};
uintptr_t (*decode)(const void*, jobject) = nullptr;
bool (*observe_async)(void*) = nullptr;
jvmtiEnv* env = nullptr;
jobject marker = nullptr;
std::atomic<bool> prepared{false}, proven{false}, attempted{false}, committed{false};
auto& active = pc_active;
std::atomic<uint64_t> epoch{1};
std::atomic<const char*> failure{"not prepared"};
thread_local bool proving = false, probe_transaction = false;
thread_local uint32_t proof_hits = 0, proof_throws = 0;
struct Pending { void* frame; uint32_t at, next; bool rethrow; };
// A nested callee consumes its continuation before returning to the outer
// event. Keep a bounded stack nevertheless: no allocation in interpreter hooks.
thread_local Pending pending[64];
thread_local size_t depth = 0;
struct Decision { uint64_t epoch = 0; void* caller = nullptr; uint32_t index = 0; int kind = 0; void* target = nullptr; };
thread_local Decision decisions[8];

void*& ExceptionSlot(void* self) { return *services.exception(self); }
bool Push(void* self, void* frame, uint32_t at, uint32_t next, bool rethrow) {
  continuation_profile::MarkerStart();
  if (depth == 64) {
    services.throw_exception(self, "Ljava/lang/InternalError;", "private continuation nesting limit");
    return false;
  }
  pending[depth++] = {frame, at, next, rethrow};
  if (!rethrow) ExceptionSlot(self) = reinterpret_cast<void*>(decode(self, marker));
  return false;
}
}
void Configure(Services value) { services = value; }
void ConfigureEnvironment(jvmtiEnv* value, bool callbacks_ready) { if (!Prepared() && callbacks_ready) env = value; }
void Invalidate() { epoch.fetch_add(1, std::memory_order_release); }
bool Prepared() { return prepared.load(std::memory_order_acquire); }
bool Proven() { return proven.load(std::memory_order_acquire); }
bool Active() { return active.load(std::memory_order_acquire); }
const char* Failure() { return failure.load(std::memory_order_acquire); }
bool ProbeTransaction(const std::string& descriptor) {
  return proving && probe_transaction &&
      (descriptor == "Lcom/github/ytlog/artdcevm/internal/PrivateContinuationProbe$Subject;" ||
       descriptor == "Lcom/github/ytlog/artdcevm/internal/PrivateContinuationProbe$Contract;");
}
bool Prepare(JavaVM*, JNIEnv* jni) {
  if (Prepared()) return true;
  if (ARTDCEVM_PRIVATE_SELF_MASK()) { failure = "disabled by debug admission mask"; return false; }
  if (android_get_device_api_level() != 33) { failure = "requires API 33"; return false; }
  if (!services.ready || !interpreter_events::Prepare() || !interpreter_events::ExceptionService() || interpreter_events::Modern()) {
    failure = "shared interpreter services unavailable"; return false;
  }
  using namespace symbols::private_continuation;
  decode = reinterpret_cast<decltype(decode)>(symbols::Find(kDecode));
  observe_async = reinterpret_cast<decltype(observe_async)>(symbols::Find(kObserveAsync));
  for (size_t i = 0; i < 4; ++i) calls[i] = reinterpret_cast<Call>(symbols::Find(kCalls[i]));
  if (!decode || !observe_async || !calls[0] || !calls[1] || !calls[2] || !calls[3]) {
    failure = "named continuation services unavailable"; return false;
  }
  if (!env) { failure = "engine event callbacks unavailable"; return false; }
  jvmtiCapabilities caps{};
  caps.can_generate_breakpoint_events = 1;
  caps.can_redefine_classes = 1;
  if (!env || env->AddCapabilities(&caps)) { failure = "continuation capabilities unavailable"; return false; }
  if (!marker) {
    jclass type = jni->FindClass("java/lang/InternalError");
    jmethodID ctor = type ? jni->GetMethodID(type, "<init>", "()V") : nullptr;
    jobject object = ctor ? jni->NewObject(type, ctor) : nullptr;
    if (object) marker = jni->NewGlobalRef(object);
    jni->DeleteLocalRef(object); jni->DeleteLocalRef(type);
  }
  if (!marker || jni->ExceptionCheck()) { failure = "continuation marker unavailable"; return false; }
  prepared.store(true, std::memory_order_release);
  failure = "runtime self-proof pending";
  return true;
}
bool Prove(JNIEnv* jni) {
  if (Proven()) return true;
  if (!Prepared() || proving || attempted.exchange(true, std::memory_order_acq_rel)) return false;
  jclass type = jni->FindClass("com/github/ytlog/artdcevm/internal/PrivateContinuationProbe");
  jmethodID run = type ? jni->GetStaticMethodID(type, "run", "()V") : nullptr;
  if (!run || jni->ExceptionCheck()) { failure = "internal self-proof unavailable"; return false; }
  if (interpreter_events::PinNotifications(env)) {
    failure = "continuation listener activation failed"; jni->DeleteLocalRef(type); return false;
  }
  active.store(true, std::memory_order_release);
  proof_hits = proof_throws = 0;
  proving = true;
  jni->CallStaticVoidMethod(type, run);
  proving = false;
  const bool ok = !jni->ExceptionCheck() && proof_hits >= 12 && proof_throws >= 2 && services.ready();
  if (!ok) {
    if (jni->ExceptionCheck()) jni->ExceptionClear();
    failure = "runtime continuation self-proof failed";
    // A committed nested proof cannot be undone by a later failed attempt.
    if (!committed.load(std::memory_order_acquire) &&
        interpreter_events::ReleaseNotifications(env) == JVMTI_ERROR_NONE)
      active.store(false, std::memory_order_release);
  } else {
    proven.store(true, std::memory_order_release);
    failure = "";
  }
  jni->DeleteLocalRef(type);
  return ok;
}
namespace {
// Keep emulated TLS and proof services out of unrelated PC events.
__attribute__((noinline)) bool Continue(void* self, const Accessor* accessor, Frame* frame, uint32_t pc,
                                      const uint16_t* insn, uint8_t opcode, int kind, uint32_t flags) {
  if ((!Proven() && !proving) || !services.ready() || (frame->frame_flags & 2)) return true;
  const uint64_t generation = epoch.load(std::memory_order_acquire);
  Decision& decision = decisions[(insn[1] ^ (reinterpret_cast<uintptr_t>(frame->method) >> 4)) & 7];
  void* target = nullptr;
  if (decision.epoch == generation && decision.caller == frame->method && decision.index == insn[1] && decision.kind == kind) {
    target = decision.target;
  } else {
    // Resolution may suspend. ART owns and roots the real ShadowFrame. An
    // ART commit or new prepared class invalidates decisions, including before
    // a method/loader address could be reused. Never cache a failed lookup.
    target = services.resolve(self, insn[1], frame->method, kind);
    if (!target || !services.kind(self, frame->method, insn[1], kind)) {
      ExceptionSlot(self) = nullptr;
      return true;
    }
    const auto target_flags = static_cast<abi::arm64::ArtMethodPrefix*>(target)->access_flags.load(std::memory_order_relaxed);
    if ((target_flags & (evolution::kAccPrivate | evolution::kAccStatic | abi::arm64::kAccObsoleteMethod)) != evolution::kAccPrivate ||
        !services.own(frame->method, target)) return true;
    if (generation == epoch.load(std::memory_order_acquire))
      decision = {generation, frame->method, insn[1], kind, target};
  }
  if (observe_async(self)) return Push(self, frame, pc, pc, true);
  const bool range = opcode == 0x74 || opcode == 0x78;
  const uint32_t receiver = range ? insn[2] : insn[2] & 15;
  if (!abi::arm64::ReadFrameReference<Frame>(frame, receiver)) {
    services.throw_exception(self, "Ljava/lang/NullPointerException;", "null obsolete private self receiver");
    if (proving) ++proof_throws;
    return Push(self, frame, pc, pc, true);
  }
  uint64_t result = 0;
  constexpr uint32_t kAccSkipAccessChecks = 0x00080000;  // dex/modifiers.h, android-13.0.0_r1.
  const bool access = !(flags & kAccSkipAccessChecks);
  const bool ok = calls[(range ? 2 : 0) + access](target, self, frame, insn, insn[0], &result);
  if (frame->frame_flags & 2) return true;
  if (frame->frame_flags & 4) {
    frame->frame_flags &= ~4u;
    ExceptionSlot(self) = nullptr;
    return Push(self, frame, pc, pc, false);
  }
  if (!ok) {
    if (proving) ++proof_throws;
    return Push(self, frame, pc, pc, true);
  }
  uint32_t next = pc + 3;
  if (next < accessor->size) {
    const uint16_t move = accessor->insns[next];
    const uint8_t op = move & 0xff;
    if (op >= 0x0a && op <= 0x0c) {
      const uint32_t reg = move >> 8;
      if (reg >= frame->number_of_vregs || (op == 0x0b && reg + 1 >= frame->number_of_vregs)) {
        services.throw_exception(self, "Ljava/lang/InternalError;", "invalid continuation destination");
        return Push(self, frame, pc, pc, true);
      }
      auto* slots = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(frame) + offsetof(Frame, vregs));
      auto* roots = slots + frame->number_of_vregs;
      slots[reg] = static_cast<uint32_t>(result);
      roots[reg] = op == 0x0c ? static_cast<uint32_t>(result) : 0;
      if (op == 0x0b) { slots[reg+1] = static_cast<uint32_t>(result >> 32); roots[reg+1] = 0; }
      ++next;
    }
  }
  if (proving) ++proof_hits;
  return Push(self, frame, pc, next, false);
}
}
bool Event(void* self, const void* raw_accessor, const void* raw_frame, uint32_t pc,
           const void*, void*, bool passed) {
  if (!passed || !Active()) return passed;
  auto* frame = static_cast<Frame*>(const_cast<void*>(raw_frame));
  const auto flags = static_cast<abi::arm64::ArtMethodPrefix*>(frame->method)->access_flags.load(std::memory_order_relaxed);
  if (!(flags & abi::arm64::kAccObsoleteMethod)) return true;
  const auto* accessor = static_cast<const Accessor*>(raw_accessor);
  if (pc > accessor->size || accessor->size - pc < 3 || accessor->registers != frame->number_of_vregs) return true;
  const auto* insn = accessor->insns + pc;
  const uint8_t opcode = insn[0] & 0xff;
  const int kind = opcode == 0x6e || opcode == 0x74 ? 2 : opcode == 0x72 || opcode == 0x78 ? 4 : -1;
  if (kind < 0) return true;
  return Continue(self, accessor, frame, pc, insn, opcode, kind, flags);
}

bool Exception(void* self, void* raw_frame, bool, bool, bool* handled) {
  if (!depth || pending[depth-1].frame != raw_frame) return false;
  const Pending current = pending[--depth];
  auto* frame = static_cast<Frame*>(raw_frame);
  const uint32_t pc = frame->dex_pc_ptr ? frame->dex_pc_ptr - frame->dex_instructions : frame->dex_pc;
  if (pc != current.at) return false;
  *handled = true;
  if (current.rethrow) return interpreter_events::MoveException(self, raw_frame, false, false);
  if (ExceptionSlot(self) != reinterpret_cast<void*>(decode(self, marker))) {
    return interpreter_events::MoveException(self, raw_frame, false, false);
  }
  ExceptionSlot(self) = nullptr;
  frame->dex_pc = current.next; frame->dex_pc_ptr = nullptr;
  continuation_profile::MarkerEnd();
  return true;
}
}
extern "C" JNIEXPORT jint JNICALL
Java_com_github_ytlog_artdcevm_internal_PrivateContinuationProbe_checkpoint(
    JNIEnv* jni, jclass, jclass klass, jbyteArray bytes) {
  using namespace artdcevm::private_continuation;
  if (!proving || !bytes) return JVMTI_ERROR_NOT_AVAILABLE;
  const jsize size = jni->GetArrayLength(bytes);
  std::vector<unsigned char> dex(size);
  jni->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(dex.data()));
  if (jni->ExceptionCheck()) return JVMTI_ERROR_INTERNAL;
  const bool previous = probe_transaction;
  probe_transaction = true;
  jvmtiClassDefinition definition{klass, size, dex.data()};
  const jint result = env->RedefineClasses(1, &definition);
  if (result == JVMTI_ERROR_NONE) { committed.store(true, std::memory_order_release); artdcevm::interpreter_events::CommitNotifications(); }
  probe_transaction = previous;
  return result;
}
