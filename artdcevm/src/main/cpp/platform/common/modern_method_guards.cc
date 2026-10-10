#include "platform/common/modern_method_guards.h"
#include "platform/common/native_receiver_guard.h"
#include "platform/abi/common/suspend_all.h"
#include "platform/common/runnable_executor.h"
#include "platform/abi/common/jni_access.h"
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <vector>

namespace artdcevm::retirement {
namespace {
using Scope = abi::common::RunnableJniScope;
using Throw = void (*)(void*, const char*, const char*);
using Decode = void* (*)(void*, jmethodID);
using Reflected = abi::common::FromReflectedMethod;
struct State {
  RetiredMethodRegistry* registry = nullptr;
  void* manager = nullptr;
  Throw throw_exception = nullptr;
  Decode decode = nullptr;
  Reflected reflected = nullptr;
  void* (*current)() = nullptr;
};
State state;
bool installed_complete = false;
std::atomic<size_t> rejected{0};
void *original_invoke, *original_reflect, *original_native_values,
     *original_id_values, *original_id_varargs, *original_virtual_values,
     *original_virtual_varargs, *original_check_signature;

bool Guard(void* method, void* self, void* result = nullptr) {
  if (!state.registry->Contains(method)) return false;
  if (result) {
    const uint64_t zero = 0;
    std::memcpy(result, &zero, sizeof(zero));
  }
  ++rejected;
  state.throw_exception(self, "Ljava/lang/NoSuchMethodError;", "Method removed by ART DCEVM");
  return true;
}
void Invoke(void* method, void* self, uint32_t* args, uint32_t size,
            void* result, const char* shorty) {
  if (Guard(method, self, result)) return;
  if(receiver_guard::CheckCompressed(receiver_guard::Path::NativeInvoke,self,method,args&&size>=4?args[0]:0,result))return;
  using Fn = void (*)(void*, void*, uint32_t*, uint32_t, void*, const char*);
  reinterpret_cast<Fn>(original_invoke)(method, self, args, size, result, shorty);
}
jobject Reflect(const Scope& access, jobject method, jobject receiver,
                jobject args, size_t frames) {
  void* native=state.reflected(access,method);
  if (Guard(native,access.self)||receiver_guard::CheckReference(receiver_guard::Path::ReflectMethod,access.self,native,receiver)) return nullptr;
  using Fn = jobject (*)(const Scope&, jobject, jobject, jobject, size_t);
  return reinterpret_cast<Fn>(original_reflect)(access, method, receiver, args, frames);
}
uint64_t NativeValues(const Scope& access, jobject receiver, void* method,
                      const jvalue* args) {
  if (Guard(method,access.self)||receiver_guard::CheckReference(receiver_guard::Path::JniMethod,access.self,method,receiver)) return 0;
  using Fn = uint64_t (*)(const Scope&, jobject, void*, const jvalue*);
  return reinterpret_cast<Fn>(original_native_values)(access, receiver, method, args);
}
template<bool virtual_call> uint64_t IdValues(const Scope& access, jobject receiver,
                                              jmethodID id, const jvalue* args) {
  void* native=state.decode(state.manager,id);
  if (Guard(native,access.self)||receiver_guard::CheckReference(receiver_guard::Path::JniMethod,access.self,native,receiver)) return 0;
  using Fn = uint64_t (*)(const Scope&, jobject, jmethodID, const jvalue*);
  return reinterpret_cast<Fn>(virtual_call ? original_virtual_values : original_id_values)(
      access, receiver, id, args);
}
template<bool virtual_call> uint64_t IdVarargs(const Scope& access, jobject receiver,
                                               jmethodID id, va_list args) {
  void* native=state.decode(state.manager,id);
  if (Guard(native,access.self)||receiver_guard::CheckReference(receiver_guard::Path::JniMethod,access.self,native,receiver)) return 0;
  using Fn = uint64_t (*)(const Scope&, jobject, jmethodID, va_list);
  return reinterpret_cast<Fn>(virtual_call ? original_virtual_varargs : original_id_varargs)(
      access, receiver, id, args);
}
// Fixed runtime/jni/check_jni.cc: the real ScopedObjectAccess inherits the
// independently calibrated AlreadyRunnable prefix. CheckJNI has already
// established shared mutator access before this native validation callback.
bool CheckSignature(void* check,const Scope& access,jobject receiver,jclass klass,
                    jmethodID id,uint32_t type,uint32_t invoke) {
  if(access.self==state.current()){void* native=state.decode(state.manager,id);if(Guard(native,access.self)||receiver_guard::CheckReference(receiver_guard::Path::JniMethod,access.self,native,receiver))return false;}
  using Fn=bool(*)(void*,const Scope&,jobject,jclass,jmethodID,uint32_t,uint32_t);
  return reinterpret_cast<Fn>(original_check_signature)(check,access,receiver,klass,id,type,invoke);
}
struct Calibration { State candidate; void* (*current)(); bool matched = false; };
bool Calibrate(const runnable_jni::Context& context, void* data) {
  auto& request = *static_cast<Calibration*>(data);
  request.matched = context.self != nullptr && request.current() == context.self;
  return request.matched;
}
struct Entry {
  const char* symbol;
  void* proxy;
  void** original;
  void* target;
  const char* equivalent_symbol = nullptr;
};
}

bool InstallMethodEntryGuards(HookEngine& hooks, JNIEnv* env, jobject root,
                             void* manager, RetiredMethodRegistry& registry,
                             std::string* error) {
  if (state.registry) {
    if (installed_complete && state.registry == &registry && state.manager == manager) return true;
    *error = "Method entry guards have an active owner or incomplete rollback; cannot reinstall";
    return false;
  }
  if (!env || !root || !manager || !runnable_executor::Available()) {
    *error = "Method guards require owned Runnable scope and observed JNI manager";
    return false;
  }
  Calibration calibration{};
  calibration.candidate = {&registry, manager,
      reinterpret_cast<Throw>(hooks.FindSymbol("libart.so", "_ZN3art6Thread17ThrowNewExceptionEPKcS2_")),
      reinterpret_cast<Decode>(hooks.FindSymbol("libart.so", "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID")),
      reinterpret_cast<Reflected>(hooks.FindSymbol("libart.so", abi::common::kFromReflectedMethod)),nullptr};
  calibration.current = reinterpret_cast<void* (*)()>(
      hooks.FindSymbol("libart.so", "_ZN3art6Thread14CurrentFromGdbEv"));
  if (!calibration.candidate.throw_exception || !calibration.candidate.decode ||
      !calibration.candidate.reflected || !calibration.current ||
      runnable_executor::With(env, root, Calibrate, &calibration) != runnable_jni::Result::kSuccess ||
      !calibration.matched) {
    *error = "Named method guard declarations or owned current Thread identity unavailable";
    return false;
  }
  calibration.candidate.current=calibration.current;
  Entry entries[] = {
    {"_ZN3art12_GLOBAL__N_111ScopedCheck17CheckMethodAndSigERNS_18ScopedObjectAccessEP8_jobjectP7_jclassP10_jmethodIDNS_9Primitive4TypeENS_10InvokeTypeE", reinterpret_cast<void*>(CheckSignature), &original_check_signature, nullptr},
    {"_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", reinterpret_cast<void*>(Invoke), &original_invoke, nullptr},
    {"_ZN3art12InvokeMethodILNS_11PointerSizeE8EEEP8_jobjectRKNS_33ScopedObjectAccessAlreadyRunnableES3_S3_S3_m", reinterpret_cast<void*>(Reflect), &original_reflect, nullptr,
     // Fixed Android 11/12 reflection.h declares the same parameters without
     // the PointerSize template. Both named entries use the same Guard owner.
     "_ZN3art12InvokeMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectS4_S4_m"},
    {"_ZN3art17InvokeWithJValuesIPNS_9ArtMethodEEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", reinterpret_cast<void*>(NativeValues), &original_native_values, nullptr},
    {"_ZN3art17InvokeWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", reinterpret_cast<void*>(IdValues<false>), &original_id_values, nullptr},
    {"_ZN3art17InvokeWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", reinterpret_cast<void*>(IdVarargs<false>), &original_id_varargs, nullptr},
    {"_ZN3art35InvokeVirtualOrInterfaceWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", reinterpret_cast<void*>(IdValues<true>), &original_virtual_values, nullptr},
    {"_ZN3art35InvokeVirtualOrInterfaceWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", reinterpret_cast<void*>(IdVarargs<true>), &original_virtual_varargs, nullptr},
  };
  for (auto& entry : entries) {
    entry.target = hooks.FindSymbol("libart.so", entry.symbol);
    if (!entry.target && entry.equivalent_symbol) {
      entry.target = hooks.FindSymbol("libart.so", entry.equivalent_symbol);
    }
    if (!entry.target) { *error = std::string("Missing method guard entry: ") + entry.symbol; return false; }
  }
  using namespace abi::common;
  auto construct=reinterpret_cast<SuspendConstruct>(hooks.FindSymbol("libart.so",kSuspendConstruct));
  auto destroy=reinterpret_cast<SuspendDestroy>(hooks.FindSymbol("libart.so",kSuspendDestroy));
  if(!construct||!destroy){*error="Missing native exclusive method hook installation declarations";return false;}
  SuspendAll stopped{};construct(&stopped,"artdcevm method guard installation",false);
  struct Stop {SuspendAll* value;SuspendDestroy destroy;~Stop(){destroy(value);}} stop{&stopped,destroy};
  // Hooks become visible before Install returns. Publish their durable owner
  // first; never clear it if rollback fails and an installed hook remains live.
  state = calibration.candidate;
  std::vector<void*> tokens;
  for (auto& entry : entries) {
    void* token = hooks.Install(entry.target, entry.proxy, entry.original, error);
    if (!token) {
      bool removed = true;
      for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        std::string rollback;
        if (!hooks.Remove(*it, &rollback)) { removed = false; *error += "; " + rollback; }
      }
      if (removed) state = {};
      return false;
    }
    tokens.push_back(token);
  }
  installed_complete = true;
  return true;
}
size_t RejectedMethodEntries() { return rejected.load(); }
bool MethodEntryGuardsOwn(const RetiredMethodRegistry& registry) {
  return installed_complete&&state.registry==&registry;
}
bool MethodEntryGuardsOwn(const RetiredMethodRegistry& registry,const void* manager) {
  return MethodEntryGuardsOwn(registry)&&state.manager==manager;
}
bool RejectRetiredMethod(void* method,void* self,void* result) {
  return installed_complete&&state.registry&&Guard(method,self,result);
}
}  // namespace artdcevm::retirement
