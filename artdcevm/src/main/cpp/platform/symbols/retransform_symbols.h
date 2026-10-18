#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Optional services that route RetransformClasses through the structural
// transaction. Every collected Android 11--17 arm64 runtime names both;
// presence proves the name only.
namespace artdcevm::symbols::retransform {
// OpenjdkJvmTi.cc places this wrapper in the public JVMTI function table.
inline constexpr NamedSymbol kJvmtiRetransformClasses{Library::kJvmti, "_ZN12openjdkjvmti14JvmtiFunctions18RetransformClassesEP9_jvmtiEnviPKP7_jclass", SymbolTable::kAnyNamed};
// ti_redefine.cc: Transformer::RetransformClasses passes the definitions here
// after every ClassFileLoadHook, with RedefinitionType kStructural (0) or kNormal (1).
inline constexpr NamedSymbol kRedefineClassesDirect{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner21RedefineClassesDirectEPNS_11ArtJvmTiEnvEPN3art7RuntimeEPNS3_6ThreadERKNSt3__16vectorINS_18ArtClassDefinitionENS8_9allocatorISA_EEEENS_16RedefinitionTypeEPNS8_12basic_stringIcNS8_11char_traitsIcEENSB_IcEEEE", SymbolTable::kAnyNamed};
// Android 11--12L ti_class_definition.cc. transform.cc invokes this in Native
// state when its lazy input is read. Later releases no longer have lazy maps.
inline constexpr NamedSymbol kInitializeDefinitionMemory{Library::kJvmti, "_ZNK12openjdkjvmti18ArtClassDefinition16InitializeMemoryEv", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::retransform
