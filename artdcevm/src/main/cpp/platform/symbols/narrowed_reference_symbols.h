#pragma once
#include "platform/symbols/art_symbol_compat.h"
namespace artdcevm::symbols::narrowed_references {
// ti_heap.cc, ART 11--17: the named, source-generated non-static field leaf
// called by ReplaceObjectReferences' ResizeReferenceVisitor. Explicit args:
// (const ResizeReferenceVisitor*, ObjPtr<Object>, MemberOffset, bool).
inline constexpr NamedSymbol kVisitReference{Library::kJvmti, "_ZZZN12openjdkjvmti12_GLOBAL__N_123ReplaceObjectReferencesERKNSt3__113unordered_mapIN3art6ObjPtrINS3_6mirror6ObjectEEES7_NS3_10HashObjPtrENS1_8equal_toIS7_EENS1_9allocatorINS1_4pairIKS7_S7_EEEEEEENK3$_0clEPS6_ENK22ResizeReferenceVisitorclES7_NS3_12MemberOffsetEb", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::narrowed_references
