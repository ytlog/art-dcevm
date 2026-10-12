#include "platform/common/method_retirement.h"

#include <android/log.h>
#include <atomic>
#include <cstdarg>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string_view>

#include "evolution/class_schema.h"
#include "platform/abi/arm64/common/redefinition.h"
#include "platform/common/art_dex_view.h"
#include "platform/common/art_runtime_profile.h"
#include "platform/symbols/structural_redefine_symbols.h"
// Audited declaration profiles, listed in kProfiles below.
#include "platform/abi/arm64/android36/method_retirement.h"
#include "platform/symbols/android36_method_retirement_symbols.h"

namespace artdcevm::method_retirement {
namespace {
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::DexFilePrefix;

// Profile-independent state. Entries are real old members whose classes are
// retained by RetainRoots, so the sets only grow. Guards reach the shared lock
// only after a retirement exists and the cheap per-method checks pass.
std::atomic<bool> any_retired{false};
std::shared_mutex retired_mutex;
std::set<const void*> retired_methods;
std::set<jmethodID> retired_ids;
std::mutex roots_mutex;
std::vector<jobject> retained_roots;
std::atomic<uint64_t> check_jni_retirement_guards{0};
bool available = false;
// Written by the mapping hook and read by the visitor within one Update call.
thread_local bool in_update = false;
thread_local std::set<const void*> copied_resolution_changes;

bool AnyRetired() { return any_retired.load(std::memory_order_acquire); }
bool ContainsMethod(const void* method) {
  if (!AnyRetired()) return false;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  return retired_methods.count(method) != 0;
}
// Never dereferences the ID, so CheckJNI can still diagnose invalid IDs.
bool ContainsId(jmethodID id) {
  if (!AnyRetired()) return false;
  std::shared_lock<std::shared_mutex> lock(retired_mutex);
  return retired_ids.count(id) != 0 ||
      ((reinterpret_cast<uintptr_t>(id) & 1) == 0 && retired_methods.count(id) != 0);
}
void Retire(const void* method) {
  std::unique_lock<std::shared_mutex> lock(retired_mutex);
  retired_methods.insert(method);
  any_retired.store(true, std::memory_order_release);
}
void RecordId(const void* method, jmethodID id) {
  std::unique_lock<std::shared_mutex> lock(retired_mutex);
  if (retired_methods.count(method)) retired_ids.insert(id);
}
uint32_t Reference(const void* object) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
}

// Hooks over one audited declaration profile. `Abi` supplies the fixed source
// declarations and build IDs; `Symbols` the names that only this profile uses.
template <typename Abi, typename Symbols> class Backend {
 public:
  static bool Prepare(std::vector<Hook>* hooks, std::string* failure) {
    if (!Abi::template PointerMap<ArtMethod>::Compatible() ||
        !Abi::template PointerMap<ArtField>::Compatible()) {
      *failure = "NDK pointer-map layout does not match the fixed ART libc++ ABI v1 declaration";
      return false;
    }
    using namespace symbols::structural;
    struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
    const Required required[] = {
        {kCollectNewFieldAndMethodMappings, Fn(CollectMappings), &original_mapping},
        {kUpdateClassStructurally, Fn(Update), &original_update},
        {kVisitReflectiveTargets, Fn(Visit), &original_visit},
        {kArtMethodInvoke, Fn(Invoke), &original_invoke},
        {Symbols::kInvokeReflection, Fn(Reflection), &original_reflection},
        {kMethodHandleInvoke, Fn(MethodHandleInvoke), &original_mh},
        {kMethodHandleInvokeExact, Fn(MethodHandleInvokeExact), &original_mh_exact},
        {Symbols::kMethodHandleInvokeExactWithFrame, Fn(MethodHandleFrame), &original_mh_frame},
        {kInvokeWithJValuesId, Fn(JValues), &original_jvalues},
        {kInvokeWithVarArgsId, Fn(VarArgs), &original_varargs},
        {kInvokeVirtualWithJValuesId, Fn(VirtualJValues), &original_virtual_jvalues},
        {kInvokeVirtualWithVarArgsId, Fn(VirtualVarArgs), &original_virtual_varargs},
        {kEncodeMethodId, Fn(Encode), &original_encode},
        {kEncodeMethodIdHandle, Fn(EncodeHandle), &original_encode_handle},
        {kInvokeObsoleteMethod, Fn(Obsolete), &original_obsolete},
        {Symbols::kEnterInterpreterFromInvoke, Fn(EnterInterpreter), &original_interpreter},
        {Symbols::kInterpreterDoCall, Fn(DoCall), &original_do_call},
        {Symbols::kInterpreterDoCallRange, Fn(DoCallRange), &original_do_call_range},
        {Symbols::kCheckJniMethodAndSig, Fn(CheckMethod), &original_check_method},
        {kThrowNewException, nullptr, Out(&throw_exception)},
        {kThrowNewWrappedException, nullptr, Out(&throw_wrapped)},
        {kFromReflectedMethod, nullptr, Out(&from_reflected)},
        {kHasSameNameAndSignature, nullptr, Out(&same_method)},
        {kFindDeclaredStaticField, nullptr, Out(&find_static_field)},
        {kFindDeclaredInstanceField, nullptr, Out(&find_instance_field)},
        {kGetCanonicalMethod, nullptr, Out(&canonical_method)},
        {kDeliverPendingException, nullptr, &deliver_pending}};
    for (const auto& entry : required) {
      void* address = symbols::Find(entry.symbol);
      if (!address) {
        *failure = std::string("missing method-retirement symbol: ") + entry.symbol.name;
        return false;
      }
      if (entry.replacement) hooks->push_back({address, entry.replacement, entry.original, entry.symbol.name});
      else *entry.original = address;
    }
    return true;
  }

 private:
  using Class = typename Abi::Class;
  using ArtField = typename Abi::ArtField;
  using ArtMethod = typename Abi::ArtMethod;
  using Handle = typename Abi::Handle;
  using RedefinitionDataIter = typename Abi::RedefinitionDataIter;
  using ScopedObjectAccess = typename Abi::ScopedObjectAccess;
  using ReflectionSourceInfo = typename Abi::ReflectionSourceInfo;
  using ReflectiveValueVisitor = typename Abi::ReflectiveValueVisitor;

  template <typename F> static void* Fn(F function) { return reinterpret_cast<void*>(function); }
  template <typename F> static void** Out(F* function) { return reinterpret_cast<void**>(function); }

  static inline void (*throw_exception)(void*, const char*, const char*) = nullptr;
  static inline void (*throw_wrapped)(void*, const char*, const char*) = nullptr;
  static inline ArtMethod* (*from_reflected)(const ScopedObjectAccess&, jobject) = nullptr;
  static inline bool (*same_method)(ArtMethod*, ArtMethod*) = nullptr;
  static inline ArtField* (*find_static_field)(Class*, std::string_view, std::string_view) = nullptr;
  static inline ArtField* (*find_instance_field)(Class*, std::string_view, std::string_view) = nullptr;
  static inline ArtMethod* (*canonical_method)(ArtMethod*, uint32_t) = nullptr;
  static inline void* deliver_pending = nullptr;
  static inline void* original_mapping = nullptr;
  static inline void* original_update = nullptr;
  static inline void* original_visit = nullptr;
  static inline void* original_invoke = nullptr;
  static inline void* original_reflection = nullptr;
  static inline void* original_mh = nullptr;
  static inline void* original_mh_exact = nullptr;
  static inline void* original_mh_frame = nullptr;
  static inline void* original_jvalues = nullptr;
  static inline void* original_varargs = nullptr;
  static inline void* original_virtual_jvalues = nullptr;
  static inline void* original_virtual_varargs = nullptr;
  static inline void* original_encode = nullptr;
  static inline void* original_encode_handle = nullptr;
  static inline void* original_obsolete = nullptr;
  static inline void* original_interpreter = nullptr;
  static inline void* original_do_call = nullptr;
  static inline void* original_do_call_range = nullptr;
  static inline void* original_check_method = nullptr;

  // Every retired member is an old method that ART marks obsolete in the same
  // commit, so live methods are rejected by one relaxed flag read.
  static bool Retired(ArtMethod* method) {
    return method && AnyRetired() &&
        (method->access_flags.load(std::memory_order_relaxed) & Abi::kAccObsoleteMethod) &&
        ContainsMethod(method);
  }
  // For paths where ART itself dereferences a pointer ID after this guard.
  static bool RetiredId(jmethodID id) {
    if (!AnyRetired()) return false;
    if (reinterpret_cast<uintptr_t>(id) & 1) return ContainsId(id);
    return Retired(reinterpret_cast<ArtMethod*>(id));
  }
  static void ThrowRetired(void* self) {
    throw_exception(self, "Ljava/lang/NoSuchMethodError;", "method retired by structural redefinition");
  }

  template <typename T> static typename Abi::template LengthPrefixedArray<T>* Members(uint64_t address) {
    return reinterpret_cast<typename Abi::template LengthPrefixedArray<T>*>(static_cast<uintptr_t>(address));
  }
  static const DexFilePrefix& ClassDex(Class* klass) {
    auto* cache = reinterpret_cast<typename Abi::DexCache*>(static_cast<uintptr_t>(klass->dex_cache));
    return *reinterpret_cast<const DexFilePrefix*>(static_cast<uintptr_t>(cache->dex_file));
  }

  // Field deletion and type/storage changes are rejected, so every old field
  // has a declared replacement found by name and type, as in ART's mapping.
  static void MapFields(Class* old_class, Class* new_class, void* field_map) {
    auto* fields = Members<ArtField>(old_class->fields);
    if (!fields) return;
    const evolution::DexView dex = DexViewOf(ClassDex(old_class));
    for (uint32_t i = 0; i < fields->length; ++i) {
      ArtField* field = fields->At(i);
      ArtField* replacement = nullptr;
      evolution::Member identity;
      std::string error;
      if (evolution::ReadFieldIdentity(dex, field->dex_field_index, &identity, &error)) {
        replacement = ((field->access_flags & evolution::kAccStatic) ? find_static_field
                                                                     : find_instance_field)(
            new_class, identity.name, identity.descriptor);
      } else {
        __android_log_print(ANDROID_LOG_ERROR, "ART-DCEVM", "field identity unavailable: %s",
                            error.c_str());
      }
      Abi::template PointerMap<ArtField>::Set(field_map, field, replacement);
    }
  }

  static void MapMethods(Class* old_class, Class* new_class, void* method_map) {
    auto* old_methods = Members<ArtMethod>(old_class->methods);
    auto* new_methods = Members<ArtMethod>(new_class->methods);
    for (uint32_t i = 0; old_methods && i < old_methods->length; ++i) {
      ArtMethod* old_method = old_methods->At(i);
      const bool declared = old_method->declaring_class == Reference(old_class);
      ArtMethod* replacement = nullptr;
      for (uint32_t j = 0; new_methods && j < new_methods->length; ++j) {
        ArtMethod* candidate = new_methods->At(j);
        // Removing an override may expose an inherited/default copied member
        // with the same identity. That does not preserve the old declaration.
        if (declared && candidate->declaring_class != Reference(new_class)) continue;
        if (same_method(old_method, candidate)) { replacement = candidate; break; }
      }
      if (!replacement && declared) {
        // The policy accepted this declaration's removal. Keep the real old
        // member so ART's visitor invariants hold; guards throw before dispatch.
        replacement = old_method;
        Retire(old_method);
      } else if (!replacement) {
        // A copied default/miranda disappears when the re-linked class gains a
        // public implementation of its slot. The interface declaration still
        // exists: map to ART's canonical method and clear cached resolutions
        // rather than retiring generated child storage.
        replacement = canonical_method(old_method, Abi::kPointerSize64);
        copied_resolution_changes.insert(old_method);
      }
      Abi::template PointerMap<ArtMethod>::Set(method_map, old_method, replacement);
    }
  }

  // Replaces ART's mapping for every batch: its own mapping CHECK-fails when a
  // copied member of a re-linked subclass disappears.
  static void CollectMappings(ClassRedefinitionPrefix*, const RedefinitionDataIter& data,
                              void* method_map, void* field_map) {
    auto* old_classes = data.Classes(true);
    auto* new_classes = data.Classes(false);
    for (int32_t i = 0; i < old_classes->length; ++i) {
      auto* old_class = old_classes->template Get<Class>(i);
      auto* new_class = new_classes->template Get<Class>(i);
      MapFields(old_class, new_class, field_map);
      MapMethods(old_class, new_class, method_map);
    }
  }

  static void Update(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) {
    struct Scope {
      bool previous = in_update;
      std::set<const void*> previous_copied = std::move(copied_resolution_changes);
      Scope() { in_update = true; copied_resolution_changes.clear(); }
      ~Scope() {
        in_update = previous;
        copied_resolution_changes = std::move(previous_copied);
      }
    } scope;
    using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
    reinterpret_cast<Function>(original_update)(self, data);
  }

  // ART marks old methods obsolete before visiting reflective targets.
  class RetirementVisitor final : public ReflectiveValueVisitor {
   public:
    explicit RetirementVisitor(ReflectiveValueVisitor* original) : original_(original) {}
    ArtMethod* VisitMethod(ArtMethod* method, const ReflectionSourceInfo& info) override {
      const bool retired = Retired(method);
      if (info.type == Abi::kSourceDexCacheResolvedMethod &&
          (retired || copied_resolution_changes.count(method))) return nullptr;
      if (retired && info.type == Abi::kSourceJniMethodId) {
        const auto& source = reinterpret_cast<const typename Abi::JniMethodIdSourceInfo&>(info);
        RecordId(method, source.id);
      }
      return original_->VisitMethod(method, info);
    }
    ArtField* VisitField(ArtField* field, const ReflectionSourceInfo& info) override {
      return original_->VisitField(field, info);
    }
   private:
    ReflectiveValueVisitor* original_;
  };
  static void Visit(void* runtime, ReflectiveValueVisitor* visitor) {
    using Function = void (*)(void*, ReflectiveValueVisitor*);
    if (!in_update) { reinterpret_cast<Function>(original_visit)(runtime, visitor); return; }
    RetirementVisitor retirement(visitor);
    reinterpret_cast<Function>(original_visit)(runtime, &retirement);
  }

  static void Invoke(ArtMethod* method, void* self, uint32_t* args, uint32_t size,
                     uint64_t* result, const char* shorty) {
    if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return; }
    using Function = void (*)(ArtMethod*, void*, uint32_t*, uint32_t, uint64_t*, const char*);
    reinterpret_cast<Function>(original_invoke)(method, self, args, size, result, shorty);
  }
  static jobject Reflection(const ScopedObjectAccess& soa, jobject reflected, jobject receiver,
                            jobject args, size_t frames) {
    if (Retired(from_reflected(soa, reflected))) {
      ThrowRetired(soa.self);
      throw_wrapped(soa.self, "Ljava/lang/reflect/InvocationTargetException;", nullptr);
      return nullptr;
    }
    using Function = jobject (*)(const ScopedObjectAccess&, jobject, jobject, jobject, size_t);
    return reinterpret_cast<Function>(original_reflection)(soa, reflected, receiver, args, frames);
  }
  // Transformer handles carry no ArtMethod, so the target is only looked up.
  static bool RetiredHandle(Handle handle) {
    const auto* target = handle.template Get<typename Abi::MethodHandle>();
    return target && target->handle_kind < Abi::kFirstAccessorKind &&
        ContainsMethod(reinterpret_cast<const void*>(static_cast<uintptr_t>(target->art_field_or_method)));
  }
  static bool HandleCall(void* original, void* self, void* frame, Handle method, Handle type,
                         const void* operands, uint64_t* result) {
    if (RetiredHandle(method)) { ThrowRetired(self); if (result) *result = 0; return false; }
    using Function = bool (*)(void*, void*, Handle, Handle, const void*, uint64_t*);
    return reinterpret_cast<Function>(original)(self, frame, method, type, operands, result);
  }
  static bool MethodHandleInvoke(void* self, void* frame, Handle method, Handle type,
                                 const void* operands, uint64_t* result) {
    return HandleCall(original_mh, self, frame, method, type, operands, result);
  }
  static bool MethodHandleInvokeExact(void* self, void* frame, Handle method, Handle type,
                                      const void* operands, uint64_t* result) {
    return HandleCall(original_mh_exact, self, frame, method, type, operands, result);
  }
  static void MethodHandleFrame(void* self, Handle method, Handle frame) {
    if (RetiredHandle(method)) { ThrowRetired(self); return; }
    using Function = void (*)(void*, Handle, Handle);
    reinterpret_cast<Function>(original_mh_frame)(self, method, frame);
  }
  template <typename Args>
  static uint64_t JniCall(void* original, const ScopedObjectAccess& soa, jobject receiver,
                          jmethodID method, Args args) {
    if (RetiredId(method)) { ThrowRetired(soa.self); return 0; }
    using Function = uint64_t (*)(const ScopedObjectAccess&, jobject, jmethodID, Args);
    return reinterpret_cast<Function>(original)(soa, receiver, method, args);
  }
  static uint64_t JValues(const ScopedObjectAccess& soa, jobject receiver, jmethodID method,
                          const jvalue* args) {
    return JniCall(original_jvalues, soa, receiver, method, args);
  }
  static uint64_t VarArgs(const ScopedObjectAccess& soa, jobject receiver, jmethodID method,
                          va_list args) {
    return JniCall<va_list>(original_varargs, soa, receiver, method, args);
  }
  static uint64_t VirtualJValues(const ScopedObjectAccess& soa, jobject receiver,
                                 jmethodID method, const jvalue* args) {
    return JniCall(original_virtual_jvalues, soa, receiver, method, args);
  }
  static uint64_t VirtualVarArgs(const ScopedObjectAccess& soa, jobject receiver,
                                 jmethodID method, va_list args) {
    return JniCall<va_list>(original_virtual_varargs, soa, receiver, method, args);
  }
  static jmethodID Encode(void* manager, ArtMethod* method) {
    using Function = jmethodID (*)(void*, ArtMethod*);
    jmethodID id = reinterpret_cast<Function>(original_encode)(manager, method);
    if (Retired(method)) RecordId(method, id);
    return id;
  }
  struct ReflectiveHandle { ArtMethod** reference; };
  static jmethodID EncodeHandle(void* manager, ReflectiveHandle method) {
    using Function = jmethodID (*)(void*, ReflectiveHandle);
    jmethodID id = reinterpret_cast<Function>(original_encode_handle)(manager, method);
    if (Retired(*method.reference)) RecordId(*method.reference, id);
    return id;
  }
  static void* Obsolete(ArtMethod* method, void* self) {
    if (Retired(method)) {
      ThrowRetired(self);
      return reinterpret_cast<void* (*)(void*)>(deliver_pending)(self);
    }
    using Function = void* (*)(ArtMethod*, void*);
    return reinterpret_cast<Function>(original_obsolete)(method, self);
  }
  static void EnterInterpreter(void* self, ArtMethod* method, uintptr_t receiver, uint32_t* args,
                               uint64_t* result, bool stay) {
    if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return; }
    using Function = void (*)(void*, ArtMethod*, uintptr_t, uint32_t*, uint64_t*, bool);
    reinterpret_cast<Function>(original_interpreter)(self, method, receiver, args, result, stay);
  }
  // Guard a new callee before PerformCall selects an interpreter-only bridge.
  // Existing Execute/EnterInterpreterFromDeopt frames keep their old bodies.
  static bool InterpreterCall(void* original, ArtMethod* method, void* self, void* frame,
                              const void* instruction, uint16_t data, bool string_init,
                              uint64_t* result) {
    if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return false; }
    using Function = bool (*)(ArtMethod*, void*, void*, const void*, uint16_t, bool, uint64_t*);
    return reinterpret_cast<Function>(original)(method, self, frame, instruction, data,
                                                string_init, result);
  }
  static bool DoCall(ArtMethod* method, void* self, void* frame, const void* instruction,
                     uint16_t data, bool string_init, uint64_t* result) {
    return InterpreterCall(original_do_call, method, self, frame, instruction, data,
                           string_init, result);
  }
  static bool DoCallRange(ArtMethod* method, void* self, void* frame, const void* instruction,
                          uint16_t data, bool string_init, uint64_t* result) {
    return InterpreterCall(original_do_call_range, method, self, frame, instruction, data,
                           string_init, result);
  }
  static bool CheckMethod(void* check, const ScopedObjectAccess& soa, jobject receiver,
                          jclass klass, jmethodID method, int32_t type, int32_t invoke) {
    if (ContainsId(method)) {
      check_jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
      ThrowRetired(soa.self);
      return false;
    }
    using Function = bool (*)(void*, const ScopedObjectAccess&, jobject, jclass, jmethodID,
                              int32_t, int32_t);
    return reinterpret_cast<Function>(original_check_method)(check, soa, receiver, klass,
                                                             method, type, invoke);
  }
};

struct Profile {
  const BuildId* art;
  const BuildId* jvmti;
  bool (*prepare)(std::vector<Hook>*, std::string*);
};
template <typename Abi, typename Symbols> constexpr Profile Entry() {
  return {&Abi::kArtBuildId, &Abi::kJvmtiBuildId, &Backend<Abi, Symbols>::Prepare};
}
// A new profile adds its fixed declarations under platform/abi/arm64/androidNN
// and its profile-only names under platform/symbols, then one entry here.
constexpr Profile kProfiles[] = {
    Entry<abi::arm64::android36::MethodRetirementAbi,
          symbols::android36::MethodRetirementSymbols>()};
}  // namespace

bool Prepare(std::vector<Hook>* hooks, std::string* note) {
  available = false;
  for (const auto& profile : kProfiles) {
    if (!LoadedBuildIdsMatch(*profile.art, *profile.jvmti)) continue;
    if (!profile.prepare(hooks, note)) return false;
    available = true;
    return true;
  }
  *note = "method retirement disabled: loaded ART build IDs match no verified declaration profile";
  return true;
}
bool Available() { return available; }
void RetainRoots(std::vector<jobject> roots) {
  std::lock_guard<std::mutex> lock(roots_mutex);
  retained_roots.insert(retained_roots.end(), roots.begin(), roots.end());
}
bool Rooted(JNIEnv* jni, jclass klass) {
  std::vector<jobject> roots;
  {
    std::lock_guard<std::mutex> lock(roots_mutex);
    roots = retained_roots;
  }
  for (jobject root : roots) if (jni->IsSameObject(root, klass)) return true;
  return false;
}
uint64_t CheckJniRetirementGuardHits() {
  return check_jni_retirement_guards.load(std::memory_order_relaxed);
}
}  // namespace artdcevm::method_retirement
