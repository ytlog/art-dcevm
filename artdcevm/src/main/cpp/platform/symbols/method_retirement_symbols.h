#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Method-retirement entry points absent from some runtimes (inlined, or a
// build-specific .__uniq suffix). They stay out of the universal catalog; the
// runtime resolves them by exact name and keeps Phase 2 when any is missing.
namespace artdcevm::symbols::method_retirement {
inline constexpr NamedSymbol kInvokeReflection{Library::kArt, "_ZN3art12InvokeMethodILNS_11PointerSizeE8EEEP8_jobjectRKNS_33ScopedObjectAccessAlreadyRunnableES3_S3_S3_m", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodHandleInvokeExactWithFrame{Library::kArt, "_ZN3art32MethodHandleInvokeExactWithFrameEPNS_6ThreadENS_6HandleINS_6mirror12MethodHandleEEENS2_INS3_18EmulatedStackFrameEEE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kEnterInterpreterFromInvoke{Library::kArt, "_ZN3art11interpreter26EnterInterpreterFromInvokeEPNS_6ThreadEPNS_9ArtMethodENS_6ObjPtrINS_6mirror6ObjectEEEPjPNS_6JValueEb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kCheckJniMethodAndSig{Library::kArt, "_ZN3art12_GLOBAL__N_111ScopedCheck17CheckMethodAndSigERNS_18ScopedObjectAccessEP8_jobjectP7_jclassP10_jmethodIDNS_9Primitive4TypeENS_10InvokeTypeE.__uniq.99033978352804627313491551960229047428", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInterpreterDoCall{Library::kArt, "_ZN3art11interpreter6DoCallILb0EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtbPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInterpreterDoCallRange{Library::kArt, "_ZN3art11interpreter6DoCallILb1EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtbPNS_6JValueE", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::method_retirement
