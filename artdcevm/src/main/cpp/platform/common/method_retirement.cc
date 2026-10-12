#include "platform/common/method_retirement.h"
#include "platform/common/art_runtime_profile.h"
#include "platform/abi/arm64/android36/method_retirement.h"
#include "platform/symbols/structural_redefine_symbols.h"
#include "platform/symbols/android36_method_retirement_symbols.h"
#include <cstdarg>
#include <mutex>
#include <set>
#include <string_view>

namespace artdcevm::method_retirement {
namespace {
namespace fixed = abi::arm64::android36;
using fixed::ArtField;
using fixed::ArtMethod;
using fixed::Class;
using fixed::Handle;
using fixed::ReflectionSourceInfo;
using fixed::ReflectiveValueVisitor;
using fixed::RedefinitionDataIter;
using fixed::ScopedObjectAccess;
using abi::arm64::ClassRedefinitionPrefix;
using abi::arm64::DexFilePrefix;
using Throw = void (*)(void*, const char*, const char*);
using Reflect = ArtMethod* (*)(const ScopedObjectAccess&, jobject);
using SameMethod = bool (*)(ArtMethod*, ArtMethod*);
using FindField = ArtField* (*)(Class*, std::string_view, std::string_view);
Throw throw_exception = nullptr;
Throw throw_wrapped = nullptr;
Reflect from_reflected = nullptr;
SameMethod same_method = nullptr;
FindField find_static_field = nullptr;
FindField find_instance_field = nullptr;
using CanonicalMethod = ArtMethod* (*)(ArtMethod*, uint32_t);
CanonicalMethod canonical_method = nullptr;
using MirrorClass = Class* (*)(ClassRedefinitionPrefix*);
MirrorClass mirror_class = nullptr;
void* original_mapping = nullptr;
void* original_update = nullptr;
void* original_visit = nullptr;
void* original_invoke = nullptr;
void* original_reflection = nullptr;
void* original_mh = nullptr;
void* original_mh_exact = nullptr;
void* original_mh_frame = nullptr;
void* original_jvalues = nullptr;
void* original_varargs = nullptr;
void* original_virtual_jvalues = nullptr;
void* original_virtual_varargs = nullptr;
void* original_encode = nullptr;
void* original_encode_handle = nullptr;
void* original_obsolete = nullptr;
void* original_interpreter = nullptr;
void* original_do_call = nullptr;
void* original_do_call_range = nullptr;
void* original_check_method = nullptr;
void* deliver_pending = nullptr;
bool available = false;
std::atomic<uint64_t> check_jni_retirement_guards{0};

// The original transaction roots each obsolete Class from its replacement's
// ClassExt, and preserves the old DexCache. These pointers refer to actual old
// members, never fabricated ArtMethods. Guards release this lock before throws.
std::mutex retired_mutex;
std::set<ArtMethod*> retired_methods;
std::set<jmethodID> retired_ids;
std::vector<jobject> retained_roots;
thread_local bool in_update = false;
thread_local std::set<ArtMethod*> copied_resolution_changes;
struct BatchContext {
  BatchContext* previous = nullptr;
  std::map<ClassRedefinitionPrefix*, std::vector<evolution::Member>> expected;
};
thread_local BatchContext* batch_context = nullptr;

bool Retired(ArtMethod* method) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  return retired_methods.count(method) != 0;
}
bool Retired(jmethodID id) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  return retired_ids.count(id) != 0 ||
      ((reinterpret_cast<uintptr_t>(id) & 1) == 0 &&
       retired_methods.count(reinterpret_cast<ArtMethod*>(id)) != 0);
}
void RecordId(ArtMethod* method, jmethodID id) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  if (retired_methods.count(method)) retired_ids.insert(id);
}
void ThrowRetired(void* self) {
  throw_exception(self, "Ljava/lang/NoSuchMethodError;", "method retired by structural redefinition");
}

template <typename T> fixed::LengthPrefixedArray<T>* Members(uint64_t address) {
  return reinterpret_cast<fixed::LengthPrefixedArray<T>*>(static_cast<uintptr_t>(address));
}
uint32_t Read32(const uint8_t* address) {
  uint32_t result;
  std::memcpy(&result, address, sizeof(result));
  return result;
}
uint16_t Read16(const uint8_t* address) {
  uint16_t result;
  std::memcpy(&result, address, sizeof(result));
  return result;
}
// Fixed DEX Header/StringId/TypeId/FieldId declarations, including CompactDex's
// separate data section. The original ART validator has verified these DEXes.
const char* DexString(const DexFilePrefix& dex, uint32_t index) {
  const uint32_t strings = Read32(dex.begin + 0x3c);
  const uint32_t offset = Read32(dex.begin + strings + sizeof(uint32_t) * index);
  const uint8_t* cursor = dex.data_begin + offset;
  for (unsigned i = 0; i < 5 && (*cursor++ & 0x80); ++i) {}
  return reinterpret_cast<const char*>(cursor);
}
const DexFilePrefix& ClassDex(Class* klass) {
  auto* cache = reinterpret_cast<fixed::DexCache*>(static_cast<uintptr_t>(klass->dex_cache));
  return *reinterpret_cast<const DexFilePrefix*>(static_cast<uintptr_t>(cache->dex_file));
}
const char* DexType(const DexFilePrefix& dex, uint32_t index) {
  const uint32_t types = Read32(dex.begin + 0x44);
  return DexString(dex, Read32(dex.begin + types + 4 * index));
}
bool ExpectedRetirement(Class* klass, ArtMethod* method) {
  if (!batch_context) return false;
  const auto& dex = ClassDex(klass);
  const uint32_t methods = Read32(dex.begin + 0x5c);
  const uint8_t* id = dex.begin + methods + 8 * method->dex_method_index;
  const char* name = DexString(dex, Read32(id + 4));
  const uint32_t protos = Read32(dex.begin + 0x4c);
  const uint8_t* proto = dex.begin + protos + 12 * Read16(id + 2);
  std::string descriptor = "(";
  const uint32_t parameters = Read32(proto + 8);
  if (parameters) {
    const uint8_t* list = dex.data_begin + parameters;
    const uint32_t count = Read32(list);
    for (uint32_t i = 0; i < count; ++i) descriptor += DexType(dex, Read16(list + 4 + 2 * i));
  }
  descriptor += ")";
  descriptor += DexType(dex, Read32(proto + 4));
  for (const auto& [redefinition, expected] : batch_context->expected) {
    if (mirror_class(redefinition) != klass) continue;
    for (const auto& retired : expected)
      if (retired.name == name && retired.descriptor == descriptor) return true;
  }
  return false;
}
ArtField* FindReplacementField(Class* old_class, Class* new_class, ArtField* field) {
  const auto& dex = ClassDex(old_class);
  const uint32_t fields = Read32(dex.begin + 0x54);
  const uint8_t* id = dex.begin + fields + 8 * field->dex_field_index;
  const uint32_t types = Read32(dex.begin + 0x44);
  const char* name = DexString(dex, Read32(id + 4));
  const char* descriptor = DexString(dex, Read32(dex.begin + types + 4 * Read16(id + 2)));
  return ((field->access_flags & 0x0008) ? find_static_field : find_instance_field)(
      new_class, std::string_view(name), std::string_view(descriptor));
}

void CollectMappings(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data,
                     void* method_map, void* field_map) {
  if (!batch_context || batch_context->expected.empty()) {
    using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&, void*, void*);
    reinterpret_cast<Function>(original_mapping)(self, data, method_map, field_map);
    return;
  }
  auto* old_classes = data.Classes(true);
  auto* new_classes = data.Classes(false);
  for (int32_t i = 0; i < old_classes->length; ++i) {
    auto* old_class = old_classes->Get<Class>(i);
    auto* new_class = new_classes->Get<Class>(i);
    auto* fields = Members<ArtField>(old_class->fields);
    for (uint32_t j = 0; fields && j < fields->length; ++j) {
      ArtField* field = fields->At(j);
      // Field deletion/type/storage changes have already been rejected. Keep
      // ART's exact declared-field mapping and its original copying algorithm.
      fixed::LibcppV1PointerMap<ArtField>::Set(
          field_map, field, FindReplacementField(old_class, new_class, field));
    }
    auto* old_methods = Members<ArtMethod>(old_class->methods);
    auto* new_methods = Members<ArtMethod>(new_class->methods);
    for (uint32_t j = 0; old_methods && j < old_methods->length; ++j) {
      ArtMethod* old_method = old_methods->At(j);
      ArtMethod* replacement = nullptr;
      for (uint32_t k = 0; new_methods && k < new_methods->length; ++k) {
        ArtMethod* candidate = new_methods->At(k);
        // Removing an override may expose an inherited/default copied member
        // with the same identity. That does not preserve the old declaration.
        if (old_method->declaring_class == reinterpret_cast<uintptr_t>(old_class) &&
            candidate->declaring_class != reinterpret_cast<uintptr_t>(new_class)) continue;
        if (same_method(old_method, candidate)) { replacement = candidate; break; }
      }
      if (!replacement) {
        // Keep a real obsolete member so all ART visitor invariants remain
        // valid. Separate guards run before virtual dispatch/receiver checks.
        if (old_method->declaring_class == reinterpret_cast<uintptr_t>(old_class) &&
            ExpectedRetirement(old_class, old_method)) {
          replacement = old_method;
          std::lock_guard<std::mutex> lock(retired_mutex);
          retired_methods.insert(old_method);
        } else {
          // A copied interface default may disappear when a newly public
          // superclass method takes its slot. Its declaration still exists;
          // use ART's canonical interface target and let new dispatch refine
          // it, rather than falsely retiring child-owned generated storage.
          replacement = canonical_method(old_method, 8);
          copied_resolution_changes.insert(old_method);
        }
      }
      fixed::LibcppV1PointerMap<ArtMethod>::Set(method_map, old_method, replacement);
    }
  }
}

void Update(ClassRedefinitionPrefix* self, const RedefinitionDataIter& data) {
  struct Scope {
    bool previous = in_update;
    std::set<ArtMethod*> previous_copied = std::move(copied_resolution_changes);
    Scope() { in_update = true; copied_resolution_changes.clear(); }
    ~Scope() {
      in_update = previous;
      copied_resolution_changes = std::move(previous_copied);
    }
  } scope;
  using Function = void (*)(ClassRedefinitionPrefix*, const RedefinitionDataIter&);
  reinterpret_cast<Function>(original_update)(self, data);
}

class RetirementVisitor final : public ReflectiveValueVisitor {
 public:
  explicit RetirementVisitor(ReflectiveValueVisitor* original) : original_(original) {}
  ArtMethod* VisitMethod(ArtMethod* method, const ReflectionSourceInfo& info) override {
    if (info.type == fixed::kSourceDexCacheResolvedMethod &&
        copied_resolution_changes.count(method)) return nullptr;
    if (Retired(method)) {
      if (info.type == fixed::kSourceDexCacheResolvedMethod) return nullptr;
      if (info.type == fixed::kSourceJniMethodId) {
        const auto& source = reinterpret_cast<const fixed::JniMethodIdSourceInfo&>(info);
        RecordId(method, source.id);
      }
    }
    return original_->VisitMethod(method, info);
  }
  ArtField* VisitField(ArtField* field, const ReflectionSourceInfo& info) override {
    return original_->VisitField(field, info);
  }
 private:
  ReflectiveValueVisitor* original_;
};
void Visit(void* runtime, ReflectiveValueVisitor* visitor) {
  using Function = void (*)(void*, ReflectiveValueVisitor*);
  if (!in_update) { reinterpret_cast<Function>(original_visit)(runtime, visitor); return; }
  RetirementVisitor retirement(visitor);
  reinterpret_cast<Function>(original_visit)(runtime, &retirement);
}

void Invoke(ArtMethod* method, void* self, uint32_t* args, uint32_t size,
            uint64_t* result, const char* shorty) {
  if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return; }
  using Function = void (*)(ArtMethod*, void*, uint32_t*, uint32_t, uint64_t*, const char*);
  reinterpret_cast<Function>(original_invoke)(method, self, args, size, result, shorty);
}
jobject Reflection(const ScopedObjectAccess& soa, jobject reflected, jobject receiver,
                   jobject args, size_t frames) {
  if (Retired(from_reflected(soa, reflected))) {
    ThrowRetired(soa.self);
    throw_wrapped(soa.self, "Ljava/lang/reflect/InvocationTargetException;", nullptr);
    return nullptr;
  }
  using Function = jobject (*)(const ScopedObjectAccess&, jobject, jobject, jobject, size_t);
  return reinterpret_cast<Function>(original_reflection)(soa, reflected, receiver, args, frames);
}
bool RetiredHandle(Handle handle) {
  const auto* target = handle.Get<fixed::MethodHandle>();
  // Kinds 0..7 invoke methods (including transformers); kinds 8+ access fields.
  return target && target->handle_kind < 8 && Retired(
      reinterpret_cast<ArtMethod*>(static_cast<uintptr_t>(target->art_field_or_method)));
}
bool HandleCall(void* original, void* self, void* frame, Handle method, Handle type,
                const void* operands, uint64_t* result) {
  if (RetiredHandle(method)) { ThrowRetired(self); if (result) *result = 0; return false; }
  using Function = bool (*)(void*, void*, Handle, Handle, const void*, uint64_t*);
  return reinterpret_cast<Function>(original)(self, frame, method, type, operands, result);
}
bool MethodHandleInvoke(void* self, void* frame, Handle method, Handle type,
                        const void* operands, uint64_t* result) {
  return HandleCall(original_mh, self, frame, method, type, operands, result);
}
bool MethodHandleInvokeExact(void* self, void* frame, Handle method, Handle type,
                             const void* operands, uint64_t* result) {
  return HandleCall(original_mh_exact, self, frame, method, type, operands, result);
}
void MethodHandleFrame(void* self, Handle method, Handle frame) {
  if (RetiredHandle(method)) { ThrowRetired(self); return; }
  using Function = void (*)(void*, Handle, Handle);
  reinterpret_cast<Function>(original_mh_frame)(self, method, frame);
}
uint64_t JValues(const ScopedObjectAccess& soa, jobject receiver, jmethodID method,
                 const jvalue* args) {
  if (Retired(method)) { ThrowRetired(soa.self); return 0; }
  using Function = uint64_t (*)(const ScopedObjectAccess&, jobject, jmethodID, const jvalue*);
  return reinterpret_cast<Function>(original_jvalues)(soa, receiver, method, args);
}
uint64_t VarArgs(const ScopedObjectAccess& soa, jobject receiver, jmethodID method, va_list args) {
  if (Retired(method)) { ThrowRetired(soa.self); return 0; }
  using Function = uint64_t (*)(const ScopedObjectAccess&, jobject, jmethodID, va_list);
  return reinterpret_cast<Function>(original_varargs)(soa, receiver, method, args);
}
uint64_t VirtualJValues(const ScopedObjectAccess& soa, jobject receiver, jmethodID method,
                        const jvalue* args) {
  if (Retired(method)) { ThrowRetired(soa.self); return 0; }
  using Function = uint64_t (*)(const ScopedObjectAccess&, jobject, jmethodID, const jvalue*);
  return reinterpret_cast<Function>(original_virtual_jvalues)(soa, receiver, method, args);
}
uint64_t VirtualVarArgs(const ScopedObjectAccess& soa, jobject receiver, jmethodID method,
                        va_list args) {
  if (Retired(method)) { ThrowRetired(soa.self); return 0; }
  using Function = uint64_t (*)(const ScopedObjectAccess&, jobject, jmethodID, va_list);
  return reinterpret_cast<Function>(original_virtual_varargs)(soa, receiver, method, args);
}
jmethodID Encode(void* manager, ArtMethod* method) {
  using Function = jmethodID (*)(void*, ArtMethod*);
  jmethodID id = reinterpret_cast<Function>(original_encode)(manager, method);
  RecordId(method, id);
  return id;
}
struct ReflectiveHandle { ArtMethod** reference; };
jmethodID EncodeHandle(void* manager, ReflectiveHandle method) {
  using Function = jmethodID (*)(void*, ReflectiveHandle);
  jmethodID id = reinterpret_cast<Function>(original_encode_handle)(manager, method);
  RecordId(*method.reference, id);
  return id;
}
void* Obsolete(ArtMethod* method, void* self) {
  if (Retired(method)) {
    ThrowRetired(self);
    return reinterpret_cast<void* (*)(void*)>(deliver_pending)(self);
  }
  using Function = void* (*)(ArtMethod*, void*);
  return reinterpret_cast<Function>(original_obsolete)(method, self);
}
void EnterInterpreter(void* self, ArtMethod* method, uintptr_t receiver, uint32_t* args,
                      uint64_t* result, bool stay) {
  if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return; }
  using Function = void (*)(void*, ArtMethod*, uintptr_t, uint32_t*, uint64_t*, bool);
  reinterpret_cast<Function>(original_interpreter)(self, method, receiver, args, result, stay);
}
bool InterpreterCall(void* original, ArtMethod* method, void* self, void* frame,
                      const void* instruction, uint16_t data, bool string_init,
                      uint64_t* result) {
  // Guard a new callee before PerformCall selects an interpreter-only bridge.
  // Existing Execute/EnterInterpreterFromDeopt frames keep their old bodies.
  if (Retired(method)) { ThrowRetired(self); if (result) *result = 0; return false; }
  using Function = bool (*)(ArtMethod*, void*, void*, const void*, uint16_t, bool, uint64_t*);
  return reinterpret_cast<Function>(original)(method, self, frame, instruction, data,
                                              string_init, result);
}
bool DoCall(ArtMethod* method, void* self, void* frame, const void* instruction,
             uint16_t data, bool string_init, uint64_t* result) {
  return InterpreterCall(original_do_call, method, self, frame, instruction, data,
                          string_init, result);
}
bool DoCallRange(ArtMethod* method, void* self, void* frame, const void* instruction,
                  uint16_t data, bool string_init, uint64_t* result) {
  return InterpreterCall(original_do_call_range, method, self, frame, instruction, data,
                          string_init, result);
}
bool CheckMethod(void* check, const ScopedObjectAccess& soa, jobject receiver, jclass klass,
                 jmethodID method, int32_t type, int32_t invoke) {
  if (Retired(method)) {
    check_jni_retirement_guards.fetch_add(1, std::memory_order_relaxed);
    ThrowRetired(soa.self);
    return false;
  }
  using Function = bool (*)(void*, const ScopedObjectAccess&, jobject, jclass, jmethodID,
                           int32_t, int32_t);
  return reinterpret_cast<Function>(original_check_method)(check, soa, receiver, klass,
                                                          method, type, invoke);
}
}  // namespace

bool Prepare(int api, std::vector<Hook>* hooks, std::string* failure) {
  available = false;
  if (api != 36) return true;
  if (!MatchesRuntimeProfile(failure)) return true;
  if (!fixed::LibcppV1PointerMap<ArtMethod>::Compatible() ||
      !fixed::LibcppV1PointerMap<ArtField>::Compatible()) {
    *failure = "NDK pointer-map layout does not match the fixed ART libc++ ABI v1 declaration";
    return false;
  }
  using namespace symbols::structural;
  using namespace symbols::android36;
  struct Required { symbols::NamedSymbol symbol; void* replacement; void** original; };
  const Required required[] = {
      {kCollectNewFieldAndMethodMappings, reinterpret_cast<void*>(CollectMappings), &original_mapping},
      {kUpdateClassStructurally, reinterpret_cast<void*>(Update), &original_update},
      {kVisitReflectiveTargets, reinterpret_cast<void*>(Visit), &original_visit},
      {kArtMethodInvoke, reinterpret_cast<void*>(Invoke), &original_invoke},
      {kInvokeReflection, reinterpret_cast<void*>(Reflection), &original_reflection},
      {kMethodHandleInvoke, reinterpret_cast<void*>(MethodHandleInvoke), &original_mh},
      {kMethodHandleInvokeExact, reinterpret_cast<void*>(MethodHandleInvokeExact), &original_mh_exact},
      {kMethodHandleInvokeExactWithFrame, reinterpret_cast<void*>(MethodHandleFrame), &original_mh_frame},
      {kInvokeWithJValuesId, reinterpret_cast<void*>(JValues), &original_jvalues},
      {kInvokeWithVarArgsId, reinterpret_cast<void*>(VarArgs), &original_varargs},
      {kInvokeVirtualWithJValuesId, reinterpret_cast<void*>(VirtualJValues), &original_virtual_jvalues},
      {kInvokeVirtualWithVarArgsId, reinterpret_cast<void*>(VirtualVarArgs), &original_virtual_varargs},
      {kEncodeMethodId, reinterpret_cast<void*>(Encode), &original_encode},
      {kEncodeMethodIdHandle, reinterpret_cast<void*>(EncodeHandle), &original_encode_handle},
      {kInvokeObsoleteMethod, reinterpret_cast<void*>(Obsolete), &original_obsolete},
      {kEnterInterpreterFromInvoke, reinterpret_cast<void*>(EnterInterpreter), &original_interpreter},
      {kInterpreterDoCall, reinterpret_cast<void*>(DoCall), &original_do_call},
      {kInterpreterDoCallRange, reinterpret_cast<void*>(DoCallRange), &original_do_call_range},
      {kCheckJniMethodAndSig, reinterpret_cast<void*>(CheckMethod), &original_check_method},
      {kThrowNewException, nullptr, reinterpret_cast<void**>(&throw_exception)},
      {kThrowNewWrappedException, nullptr, reinterpret_cast<void**>(&throw_wrapped)},
      {kFromReflectedMethod, nullptr, reinterpret_cast<void**>(&from_reflected)},
      {kHasSameNameAndSignature, nullptr, reinterpret_cast<void**>(&same_method)},
      {kFindDeclaredStaticField, nullptr, reinterpret_cast<void**>(&find_static_field)},
      {kFindDeclaredInstanceField, nullptr, reinterpret_cast<void**>(&find_instance_field)},
      {kGetCanonicalMethod, nullptr, reinterpret_cast<void**>(&canonical_method)},
      {kClassRedefinitionGetMirrorClass, nullptr, reinterpret_cast<void**>(&mirror_class)},
      {kDeliverPendingException, nullptr, &deliver_pending}};
  for (const auto& entry : required) {
    void* address = symbols::Find(entry.symbol);
    if (!address) {
      *failure = std::string("missing Android 16 method-retirement symbol: ") + entry.symbol.name;
      return false;
    }
    if (entry.replacement) {
      hooks->push_back({address, entry.replacement, entry.original, entry.symbol.name});
    } else {
      *entry.original = address;
    }
  }
  available = true;
  return true;
}
bool Available() { return available; }
void RetainRoots(std::vector<jobject> roots) {
  std::lock_guard<std::mutex> lock(retired_mutex);
  retained_roots.insert(retained_roots.end(), roots.begin(), roots.end());
}
bool Rooted(JNIEnv* jni, jclass klass) {
  std::vector<jobject> roots;
  {
    std::lock_guard<std::mutex> lock(retired_mutex);
    roots = retained_roots;
  }
  for (jobject root : roots) if (jni->IsSameObject(root, klass)) return true;
  return false;
}
BatchScope::BatchScope() : state_(new BatchContext{batch_context, {}}) {
  batch_context = static_cast<BatchContext*>(state_);
}
BatchScope::~BatchScope() {
  auto* context = static_cast<BatchContext*>(state_);
  batch_context = context->previous;
  delete context;
}
void RegisterRetirements(ClassRedefinitionPrefix* self, std::vector<evolution::Member> methods) {
  if (!batch_context) return;
  if (methods.empty()) batch_context->expected.erase(self);
  else batch_context->expected[self] = std::move(methods);
}
void InvalidateEnclosingBatches() {
  for (auto* context = batch_context ? batch_context->previous : nullptr;
       context; context = context->previous) context->expected.clear();
}
uint64_t CheckJniRetirementGuardHits() {
  return check_jni_retirement_guards.load(std::memory_order_relaxed);
}
}  // namespace artdcevm::method_retirement
