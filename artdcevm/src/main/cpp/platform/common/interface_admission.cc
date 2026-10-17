#include "platform/common/interface_redefinition_internal.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
namespace detail {
// Observe ART's existing scope chain. No candidate root is installed, no object
// is dereferenced, and the callback only records addresses into fixed storage.
class ScopeRoots final : public RootVisitor {
 public:
  static constexpr size_t kMaxRoots = 64;
  static constexpr size_t kMaxHeader = kMaxInterfaceScopeHeader;
  explicit ScopeRoots(void* scope, size_t capacity)
      : begin(reinterpret_cast<uintptr_t>(scope)), end(begin + kMaxHeader + 4 * capacity) {}
  void VisitRoots(void***, size_t, const RootInfo&) noexcept override { unexpected = true; }
  void VisitRoots(uint32_t** roots, size_t count, const RootInfo&) noexcept override {
    for (size_t i = 0; i < count; ++i) {
      const uintptr_t address = reinterpret_cast<uintptr_t>(roots[i]);
      if (address >= begin && address < end && (address - begin) % 4 == 0)
        observed[(address - begin) / 4] = true;
    }
  }
  bool Matches(const InterfaceRuntime& layout, void* scope, const uint32_t* target,
               uint32_t size) const {
    const auto* start = static_cast<uint8_t*>(scope) + layout.scope_header_size;
    const uintptr_t address = reinterpret_cast<uintptr_t>(target);
    if (unexpected || address < reinterpret_cast<uintptr_t>(start) ||
        address >= reinterpret_cast<uintptr_t>(start + 4 * size)) return false;
    for (size_t offset = 0; begin + offset < end; offset += 4) {
      const bool expected = offset >= layout.scope_header_size &&
          offset < layout.scope_header_size + 4 * size &&
          Field<uint32_t>(scope, offset) != 0;
      if (observed[offset / 4] != expected) return false;
    }
    return observed[(address - begin) / 4];
  }
 private:
  uintptr_t begin, end;
  std::array<bool, kMaxRoots + kMaxHeader / 4> observed{};
  bool unexpected = false;
};
bool VerifyScopes(const InterfaceRuntime& layout, const RuntimeThread& thread_layout, void* thread, Handle target) {
  void* scope = Field<void*>(thread, thread_layout.top_handle_scope);
  // Both fixed sources create a StackHandleScope<1> immediately around the
  // admission call. Check its known root address BEFORE reading the candidate
  // header: a mismatched Thread slot must never be followed as a pointer.
  if (!scope || reinterpret_cast<uintptr_t>(scope) + layout.scope_header_size !=
                    reinterpret_cast<uintptr_t>(target.reference)) return false;
  const int32_t capacity = Field<int32_t>(scope, layout.scope_capacity);
  const uint32_t size = layout.scope_size == InterfaceRuntime::kAbsent
      ? capacity : Field<uint32_t>(scope, layout.scope_size);
  if (capacity != 1 || size != 1) return false;
  ScopeRoots observed(scope, capacity);
  if (!process->visit_handle_roots.Visit(layout.scope_thread_id_signed, thread, &observed,
                               Field<uint32_t>(thread, thread_layout.tid))) return false;
  return observed.Matches(layout, scope, target.reference, size);
}

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
    if (!VerifyScopes(*candidate, *thread_layout, driver.self, klass)) continue;
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
  void* object = klass.Get<void>();
  Target* target = Find(object);
  if (!target) return reinterpret_cast<Admission>(original)(klass, message);
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
    const Target* owner = Find(Decode(target->owner));
    if (!owner) return Reject(message, JVMTI_ERROR_INTERNAL, "interface pointer-id snapshot owner unavailable");
    for (size_t i = 0; i < owner->snapshot.types.size(); ++i) {
      PointerMarker marker = CheckPointerMarker(Decode(owner->snapshot.types[i]));
ARTDCEVM_TEST(
      // Test the semantic result, never write a marker into a published ClassExt.
      if ((i == 0 && ARTDCEVM_FAULT(19)) || (i > 0 && ARTDCEVM_FAULT(20))) marker = PointerMarker::kMarked;
)
      if (marker == PointerMarker::kClear) continue;
      state->rejection_class = &owner->snapshot.descriptors[i];
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
