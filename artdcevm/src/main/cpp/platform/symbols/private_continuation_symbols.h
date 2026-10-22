#pragma once
#include "platform/symbols/art_symbol_compat.h"
namespace artdcevm::symbols::private_continuation {
inline constexpr NamedSymbol kMoveLegacy{Library::kArt, "_ZN3art11interpreter22MoveToExceptionHandlerEPNS_6ThreadERNS_11ShadowFrameEPKNS_15instrumentation15InstrumentationE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDecode{Library::kArt, "_ZNK3art6Thread13DecodeJObjectEP8_jobject", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObserveAsync{Library::kArt, "_ZN3art6Thread21ObserveAsyncExceptionEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kCalls[] = {
 {Library::kArt, "_ZN3art11interpreter6DoCallILb0ELb0EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE", SymbolTable::kAnyNamed},
 {Library::kArt, "_ZN3art11interpreter6DoCallILb0ELb1EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE", SymbolTable::kAnyNamed},
 {Library::kArt, "_ZN3art11interpreter6DoCallILb1ELb0EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE", SymbolTable::kAnyNamed},
 {Library::kArt, "_ZN3art11interpreter6DoCallILb1ELb1EEEbPNS_9ArtMethodEPNS_6ThreadERNS_11ShadowFrameEPKNS_11InstructionEtPNS_6JValueE", SymbolTable::kAnyNamed}};
}
