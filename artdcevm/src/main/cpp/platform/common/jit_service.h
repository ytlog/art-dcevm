#pragma once
#include <cstdint>

namespace artdcevm::jit_service {
// Named declarations from fixed ART runtime/jit/jit.h and compilation_kind.h
// for android-11.0.0_r1 through android-17.0.0_r1. These describe call ABI only;
// a provider must first authorize its runtime/profile, JIT instance, compiler
// policy, mutator lock and JIT suspension. API selection cannot calibrate them.
enum class Kind { kOsr, kBaseline, kOptimized, kFast };
using Compile11 = bool (*)(void*, void*, void*, bool, bool, bool);
enum class CompilationKind12 : int { kOsr = 0, kBaseline = 1, kOptimized = 2 };
enum class CompilationKind17 : int { kOsr = 0, kFast = 1, kBaseline = 2, kOptimized = 3 };
using Compile12 = bool (*)(void*, void*, void*, CompilationKind12, bool);
using Compile17 = bool (*)(void*, void*, void*, CompilationKind17, bool, bool);
inline constexpr const char* kCompile11 =
    "_ZN3art3jit3Jit13CompileMethodEPNS_9ArtMethodEPNS_6ThreadEbbb";
inline constexpr const char* kCompile12 =
    "_ZN3art3jit3Jit13CompileMethodEPNS_9ArtMethodEPNS_6ThreadENS_15CompilationKindEb";
inline constexpr const char* kCompile17 =
    "_ZN3art3jit3Jit13CompileMethodEPNS_9ArtMethodEPNS_6ThreadENS_15CompilationKindEbb";

class Compiler {
 public:
  // Exact one-symbol lookup, no signature fallback. Failure preserves *out.
  template<class Lookup>
  static bool Bind(int api, Lookup lookup, Compiler* out) {
    if (!out || api < 30 || api > 37) return false;
    Compiler result;
    result.api_ = api;
    if (api == 30) {
      result.legacy_ = reinterpret_cast<Compile11>(lookup(kCompile11));
      if (!result.legacy_) return false;
    } else if (api == 37) {
      result.modern_ = reinterpret_cast<Compile17>(lookup(kCompile17));
      if (!result.modern_) return false;
    } else {
      result.standard_ = reinterpret_cast<Compile12>(lookup(kCompile12));
      if (!result.standard_) return false;
    }
    *out = result;
    return true;
  }

  bool Compile(void* jit, void* method, void* self, Kind kind,
               bool prejit = false, bool dynamic_instrumentation = false) const {
    if (!jit || !method || !self || api_ == 0) return false;
    if (dynamic_instrumentation && api_ != 37) return false;
    int value;
    switch (kind) {
      case Kind::kOsr: value = 0; break;
      case Kind::kFast:
        if (api_ != 37) return false;
        value = 1;
        break;
      case Kind::kBaseline: value = api_ == 37 ? 2 : 1; break;
      case Kind::kOptimized: value = api_ == 37 ? 3 : 2; break;
      default: return false;
    }
    if (api_ == 30)
      return legacy_(jit, method, self, kind == Kind::kBaseline,
                     kind == Kind::kOsr, prejit);
    if (api_ == 37)
      return modern_(jit, method, self, static_cast<CompilationKind17>(value),
                     prejit, dynamic_instrumentation);
    return standard_(jit, method, self, static_cast<CompilationKind12>(value), prejit);
  }
 private:
  int api_ = 0;
  Compile11 legacy_ = nullptr;
  Compile12 standard_ = nullptr;
  Compile17 modern_ = nullptr;
};
}  // namespace artdcevm::jit_service
