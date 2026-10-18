#include "platform/common/interface_redefinition_internal.h"
#include "platform/common/runtime_roots.h"
#include "platform/common/invoke_kind.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
namespace detail {
// Only a failed independent declaration probe disables the process. A target
// that cannot supply the required member evidence rejects this transaction.
const char* SelectRuntime(Handle klass, const Target& target) {
  State* state = CurrentState();
  if (!runtime_thread::Ready()) {
    Disable("interface Thread declaration did not match JNI environment/tid");
    return process->disabled_reason.load();
  }
  RedefinerPrefix driver{nullptr, 0, nullptr, runtime_thread::Current(Thread())};
  ClassRedefinitionPrefix context{&driver, target.klass, nullptr};
  if (!class_layout::VerifyTarget(state->thread, &context, target.live)) {
    return "interface target is unsupported by Class member probes";
  }
  bool supported_candidate = false;
  for (int release : class_layout::CandidateReleases(true)) {
    const InterfaceRuntime* candidate = RuntimeFor(release);
    if (!candidate) continue;
    supported_candidate = true;
    const auto* thread_layout = runtime_thread::LayoutFor(Thread(), release);
    if (!thread_layout) continue;
ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT(13)) { Disable("interface Thread probe rejected injected mismatch"); return process->disabled_reason.load(); }
)
    if (!runtime_roots::VerifyScopes(*candidate, *thread_layout, driver.self, klass, process->visit_handle_roots)) continue;
ARTDCEVM_TEST(
    if (ARTDCEVM_FAULT(14)) { Disable("interface HandleScope probe rejected injected mismatch"); return process->disabled_reason.load(); }
)
    state->runtime = candidate;
    state->thread_layout = thread_layout;
    state->runtime_verified = true;
    state->selected_release = release;
    return nullptr;
  }
  if (!supported_candidate) return "interface target has no supported Class runtime candidate";
  Disable("interface HandleScope/Thread declarations did not match verified Class candidates");
  return process->disabled_reason.load();
}
enum class PointerMarker { kClear, kMarked, kUnavailable };
bool NamedReference(void* object, std::string_view name, std::string_view descriptor, void** value) {
  void* type = runtime_thread::Read(Thread(), static_cast<Object*>(object)->klass);
  const auto* field = process->find_instance_field(type, name, descriptor);
  if (!field) return false;
  *value = runtime_thread::Read(Thread(), __atomic_load_n(&Field<uint32_t>(object, field->offset), __ATOMIC_ACQUIRE));
  return true;
}
PointerMarker CheckPointerMarker(void* klass) {
  // ClassExt::Has*PointerIdMarker in both fixed sources: nonnull and not array.
  // Every managed reference offset comes from ART's named field table.
  void* ext = nullptr;
  if (!NamedReference(klass, "extData", "Ldalvik/system/ClassExt;", &ext))
    return PointerMarker::kUnavailable;
  if (!ext) return PointerMarker::kClear;
  for (const auto name : {"instanceJfieldIDs", "jmethodIDs", "staticJfieldIDs"}) {
    void* ids = nullptr;
    if (!NamedReference(ext, name, "Ljava/lang/Object;", &ids)) return PointerMarker::kUnavailable;
    if (!ids) continue;
    void* component = nullptr;
    if (!NamedReference(runtime_thread::Read(Thread(), static_cast<Object*>(ids)->klass), "componentType", "Ljava/lang/Class;", &component))
      return PointerMarker::kUnavailable;
    if (!component) return PointerMarker::kMarked;
  }
  return PointerMarker::kClear;
}
jvmtiError Admit(Handle klass, std::string* message, bool structural, size_t spelling) noexcept try {
  void* original = structural ? process->original_structural[spelling] : process->original_normal[spelling];
  if (const State* state = CurrentState(); state && !state->targets.empty() && !state->thread) {
    Disable("interface Thread declaration did not match JNI environment/tid");
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, process->disabled_reason.load());
  }
  // Ordinary targets use the same ART admission scope as interfaces. Publish
  // rooting declarations only after the native Thread and named root visitor agree.
  if (auto thread = runtime_thread::Ready()) {
    for (const auto& entry : abi::arm64::kInterfaceRuntimes) {
      const auto* layout = runtime_thread::LayoutFor(*thread, entry.release);
      if (layout && abi::arm64::IsAdmitted(entry.status))
        invoke_kind::ObserveScope(runtime_thread::Current(*thread), layout, entry.runtime, klass);
    }
  }
  void* object = klass.Get<void>();
  Target* target = Find(object);
  if (!target) {
    const auto result = reinterpret_cast<Admission>(original)(klass, message);
    State* state = CurrentState();
    if (result == JVMTI_ERROR_NONE && state && !state->runtime && !state->groups.empty() && RuntimeAvailable())
      for (const auto& ordinary : state->class_targets)
        if (Decode(ordinary.klass) == klass.Get<void>()) {
          if (const char* reason = SelectRuntime(klass, ordinary))
            return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, reason);
          break;
        }
    return result;
  }
  State* state = CurrentState();
  state->rejected_target = target;
  state->rejection_class = nullptr;
  if (!RuntimeAvailable()) return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, process->disabled_reason.load());
  if (const char* reason = SelectRuntime(klass, *target))
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, reason);
  if ((Status(object) >= ClassStatus::kInitialized) !=
      ((target->status & JVMTI_CLASS_STATUS_INITIALIZED) != 0))
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, "interface initialization state changed since snapshot");
  if (target->boot)
    return Reject(message, JVMTI_ERROR_UNMODIFIABLE_CLASS, "interface redefinition rejects boot interfaces");
  void* linker = process->class_linker.load();
  if (!linker)
    return Reject(message, JVMTI_ERROR_NOT_AVAILABLE, "interface redefinition ClassLinker unavailable");
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(8)) throw std::bad_alloc{};
)
  if (structural) {
    if (target->group >= state->groups.size())
      return Reject(message, JVMTI_ERROR_INTERNAL, "interface pointer-id snapshot owner unavailable");
    // ART's own admission of a structural ancestor checks that ancestor's subtypes.
    const TypeSnapshot& snapshot = state->groups[target->group].core.snapshot;
    for (size_t i = 0; i < snapshot.types.size(); ++i) {
      PointerMarker marker = CheckPointerMarker(Decode(snapshot.types[i]));
ARTDCEVM_TEST(
      // Test the semantic result, never write a marker into a published ClassExt.
      if ((i == 0 && ARTDCEVM_FAULT(19)) || (i > 0 && ARTDCEVM_FAULT(20))) marker = PointerMarker::kMarked;
)
      if (marker == PointerMarker::kClear) continue;
      state->rejection_class = &snapshot.descriptors[i];
      return Reject(message, marker == PointerMarker::kMarked ? JVMTI_ERROR_UNMODIFIABLE_CLASS
                                                             : JVMTI_ERROR_NOT_AVAILABLE,
                    marker == PointerMarker::kMarked ? "active pointer JNI IDs prevent structural redefinition"
                                                      : "interface pointer-id field table is unavailable");
    }
    void* thread = runtime_thread::Current(Thread());
    if (Status(object) == ClassStatus::kInitializing && Field<uint32_t>(object, state->runtime->clinit_thread_id) == Field<uint32_t>(thread, ThreadLayout().tid))
      return Reject(message, JVMTI_ERROR_INTERNAL, "interface redefinition rejects redefinition during initialization");
    if (!process->ensure_initialized(linker, thread, klass, true, true)) {
      // Thread::ClearException() in runtime/thread.h is this assignment.
      Field<void*>(thread, ThreadLayout().exception) = nullptr;
      return Reject(message, JVMTI_ERROR_INVALID_CLASS, "interface redefinition target failed initialization");
    }
    // ART can ask admission again after this hook initialized the target.
    target->status |= JVMTI_CLASS_STATUS_INITIALIZED;
  }
  return JVMTI_ERROR_NONE;
} catch (...) {
  // Even writing the diagnostic string can allocate. Do not write one here.
  if (const State* state = CurrentState(); state && state->runtime)
    Field<void*>(runtime_thread::Current(Thread()), ThreadLayout().exception) = nullptr;
  return ArtHookExceptionError();
}
jvmtiError NormalCan(Handle klass, std::string* message) noexcept { return Admit(klass, message, false, 0); }
jvmtiError NormalGet(Handle klass, std::string* message) noexcept { return Admit(klass, message, false, 1); }
jvmtiError StructuralCan(Handle klass, std::string* message) noexcept { return Admit(klass, message, true, 0); }
jvmtiError StructuralGet(Handle klass, std::string* message) noexcept { return Admit(klass, message, true, 1); }
}  // namespace detail

}  // namespace artdcevm::interface_redefinition
