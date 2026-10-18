#include "platform/common/interface_redefinition_internal.h"

namespace artdcevm::interface_redefinition {
using namespace detail;
namespace detail {
Process* process = nullptr;
thread_local Timing timing;
void Disable(const char* reason) noexcept {
  process->disabled_reason.store(reason);
  __android_log_write(ANDROID_LOG_WARN, "ART-DCEVM", reason);
}
bool RuntimeAvailable() { return process->available && process->disabled_reason.load() == nullptr; }
}  // namespace detail
void RuntimeBackend::ObserveClassLinker(void* linker) { process->class_linker.store(linker); }
bool RuntimeBackend::Available() { return RuntimeAvailable(); }
std::string RuntimeBackend::RuntimeState() {
  if (const char* reason = process->disabled_reason.load()) return std::string("disabled: ") + reason;
  if (!process->available) return "unavailable";
  const int release = process->verified_release.load();
  return release ? "verified: android" + std::to_string(release) + " Class/Thread/HandleScope/holder probes"
                 : "unverified: interface runtime declarations";
}
Timing RuntimeBackend::LastTiming() { return timing; }
std::string RuntimeBackend::TakeRejection() {
  State* state = CurrentState();
  if (!state || !state->rejection) return {};
  const char* reason = std::exchange(state->rejection, nullptr);
  const Target* target = state->rejected_target;
  if (state->rejection_class) return *state->rejection_class + ": " + reason;
  return target ? target->live.descriptor + ": " + reason : std::string(reason);
}
bool RuntimeBackend::IsTarget(ClassRedefinitionPrefix* self) { return Available() && Find(process->mirror_class(self)); }
bool RuntimeBackend::IsAffected(ClassRedefinitionPrefix* self) { return Available() && AffectedGroup(process->mirror_class(self)); }
bool RuntimeBackend::SharesImplementationGroup(ClassRedefinitionPrefix* self) {
  const State* state = CurrentState();
  if (!state) return false;
  for (const auto& group : state->groups)
    for (jclass ancestor : group.ancestors)
      if (Decode(ancestor) == process->mirror_class(self)) return true;
  return false;
}
void* RuntimeBackend::VerificationClass(void* linker, std::string_view descriptor, Handle loader) noexcept try {
  const State* state = CurrentState();
  if (!state || !state->attempt || state->targets.empty()) return nullptr;
ARTDCEVM_TEST(
  if (ARTDCEVM_FAULT(17)) throw std::bad_alloc{};
)
  // LookupClass observes the loader's table without publishing replacements or
  // caching them in any old DEX. The caller limits this to CheckVerification.
  void* old;
  if (process->lookup_view) {
    old = process->lookup_view(linker, runtime_thread::Current(Thread()), descriptor, loader.Get<void>());
  } else {
    const std::string name(descriptor);
    old = process->lookup_chars(linker, runtime_thread::Current(Thread()), name.c_str(), loader.Get<void>());
  }
  for (const auto& group : state->groups) {
    auto* replacements = ReplacementRoot(group).Get<ObjectArray>();
    const TypeSnapshot* allocated = group.Allocated();
    if (!allocated) continue;
    const auto& types = allocated->types;
    for (size_t i = 0; replacements && i < types.size(); ++i)
      if (Decode(types[i]) == old) return runtime_thread::Read(Thread(), replacements->elements[i]);
  }
  return nullptr;
} catch (...) {
  CurrentState()->attempt->error = ArtHookExceptionError();
  return nullptr;
}
jvmtiError RuntimeBackend::VerificationError() noexcept {
  const State* state = CurrentState();
  return state && state->attempt ? state->attempt->error : JVMTI_ERROR_NONE;
}
void PrepareProcess(const Services& services, std::vector<Hook>* hooks, std::string* note) {
  process->available = false;
  ARTDCEVM_TEST(fault_injection::InterfacePrepared(false);)
  process->disabled_reason.store(nullptr);
  process->verified_release.store(0);
  const auto reject = [&](const char* reason) {
    process->disabled_reason.store(reason);
    *note = reason;
  };
  const int api = android_get_device_api_level();
  if (!IsDeviceVerifiedInterfaceApi(api)) {
    // Preserve the public diagnostic while deriving the list from the registry.
    static const std::string reason = [] {
      std::string value = "interface redefinition is device-verified only on ";
      bool first = true;
      for (const auto& entry : kInterfaceRuntimes) {
        if (entry.status != InterfaceRuntimeStatus::kDeviceVerified) continue;
        value += (first ? "" : " and ") + std::string("Android ") +
            std::to_string(entry.release) + "/API " + std::to_string(entry.first_api);
        if (entry.last_api != entry.first_api) value += "–" + std::to_string(entry.last_api);
        first = false;
      }
      return value;
    }();
    reject(reason.c_str());
    return;
  }
  if (!services.verifier_redirect) {
    reject("interface redefinition requires verifier hooks and runtime-probed declarations");
    return;
  }
  using namespace symbols::interface_redefinition;
  process->visit_handle_roots.unsigned_ids = reinterpret_cast<HandleScopeVisitors::Unsigned>(symbols::Find(kHandleScopeVisitRoots));
  process->visit_handle_roots.signed_ids = reinterpret_cast<HandleScopeVisitors::Signed>(symbols::Find(kHandleScopeVisitRootsSigned));
  process->mirror_class = services.mirror_class;
  process->find_instance_field = services.find_instance_field;
  process->ensure_initialized = reinterpret_cast<Ensure>(symbols::Find(kEnsureInitialized));
  process->new_local_ref = reinterpret_cast<decltype(process->new_local_ref)>(symbols::Find(kNewLocalRef));
  process->delete_local_ref = reinterpret_cast<decltype(process->delete_local_ref)>(symbols::Find(kDeleteLocalRef));
  process->allocate_cache = reinterpret_cast<CacheAlloc>(symbols::Find(kCreateNewDexCache));
  process->decode_global = reinterpret_cast<ClassArrayRoots::DecodeGlobal>(symbols::Find(kDecodeGlobal));
  process->set_cache = reinterpret_cast<SetCache>(symbols::Find(kSetDexCache));
  process->allocate_class = reinterpret_cast<Allocate>(symbols::Find(symbols::structural::kAllocateNewClassObject));
  process->lookup_chars = reinterpret_cast<LookupChars>(symbols::Find(kLookupClassChars));
  process->lookup_view = reinterpret_cast<LookupView>(symbols::Find(kLookupClassView));
  void* referrer_lookup = symbols::Find(kLookupReferrer);
  void* cache_lookup = symbols::Find(kLookupCache);
  void* finish = symbols::Find(kFinishNewClassAllocations);
  void* run = symbols::Find(kRun);
  void* link_class = symbols::Find(kLinkClass);
  process->set_status_internal = reinterpret_cast<decltype(process->set_status_internal)>(symbols::Find(kSetStatusInternal));
  if (!services.runtime_thread || !process->visit_handle_roots.Available() || !process->mirror_class || !process->find_instance_field || !process->ensure_initialized || !process->new_local_ref || !process->delete_local_ref || !finish ||
      !process->allocate_cache || !process->decode_global || !process->set_cache || !process->allocate_class || (!process->lookup_chars && !process->lookup_view) ||
      !referrer_lookup || !cache_lookup || !run || !link_class || !process->set_status_internal) {
    reject("interface redefinition named services unavailable");
    return;
  }
  std::vector<Hook> resolved;
  const symbols::NamedSymbol names[] = {kCanNormal, kGetNormal, kCanStructural, kGetStructural};
  void* replacements[] = {reinterpret_cast<void*>(NormalCan), reinterpret_cast<void*>(NormalGet),
      reinterpret_cast<void*>(StructuralCan), reinterpret_cast<void*>(StructuralGet)};
  bool modes[2]{};
  for (size_t i = 0; i < 4; ++i) {
    if (void* address = symbols::Find(names[i])) {
      resolved.push_back({address, replacements[i], i < 2 ? &process->original_normal[i] : &process->original_structural[i - 2], names[i].name});
      modes[i / 2] = true;
    }
  }
  if (!modes[0] || !modes[1]) { reject("interface redefinition admission names unavailable"); return; }
  resolved.push_back({finish, reinterpret_cast<void*>(Finish), &process->original_finish, kFinishNewClassAllocations.name});
  resolved.push_back({run, reinterpret_cast<void*>(Run), &process->original_run, kRun.name});
  resolved.push_back({link_class, reinterpret_cast<void*>(LinkClass), &process->original_link_class, kLinkClass.name});
  resolved.push_back({referrer_lookup, reinterpret_cast<void*>(LookupByReferrer), &process->original_lookup_referrer, kLookupReferrer.name});
  resolved.push_back({cache_lookup, reinterpret_cast<void*>(LookupByCache), &process->original_lookup_cache, kLookupCache.name});
  for (const auto& name : {kThrowNonPublicGlobal, kThrowNonPublicLocal, kThrowNonPublicUnique}) {
    if (void* address = symbols::Find(name)) {
      resolved.push_back({address, reinterpret_cast<void*>(NonPublicImplementation), &process->original_non_public, name.name});
      break;
    }
  }
  hooks->insert(hooks->end(), resolved.begin(), resolved.end());
  process->available = true;
  ARTDCEVM_TEST(fault_injection::InterfacePrepared(true);)
  *note = "interface redefinition prepared; Class/Thread/HandleScope/holder runtime probes required";
}
Backend* Prepare(const Services& services, std::vector<Hook>* hooks,
                 std::string* note, std::string* unavailable_report) {
  if (!process) process = new Process;
  PrepareProcess(services, hooks, note);
  if (const char* reason = process->disabled_reason.load())
    *unavailable_report = std::string("disabled: ") + reason;
  else
    *unavailable_report = "unavailable";
  const bool ready = RuntimeAvailable();
  // Hooks already on an ART stack may still hold the published backend while
  // Install rolls back or degrades. Keep its identity for the process lifetime;
  // only the engine's optional view becomes null when services are unavailable.
  if (ready && !process->backend) process->backend = std::make_unique<RuntimeBackend>();
  return ready ? process->backend.get() : nullptr;
}

}  // namespace artdcevm::interface_redefinition
