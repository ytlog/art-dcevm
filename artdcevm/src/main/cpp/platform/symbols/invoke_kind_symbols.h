#pragma once
#include "platform/symbols/art_symbol_compat.h"

namespace artdcevm::symbols::invoke_kind {
// class_linker{,-inl}.h, Android 11--15: ResolveMode 0 skips invocation checks.
// The two overloads cover cached and cold lookups in the older interpreter.
inline constexpr NamedSymbol kResolveCaller{Library::kArt, "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kResolveCache{Library::kArt, "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEjNS_6HandleINS_6mirror8DexCacheEEENS5_INS6_11ClassLoaderEEES4_NS_10InvokeTypeE", SymbolTable::kAnyNamed};
// Android 16--17: unchecked ID resolution no longer receives an invoke type.
inline constexpr NamedSymbol kResolveMethodId{Library::kArt, "_ZN3art11ClassLinker15ResolveMethodIdEjPNS_9ArtMethodE", SymbolTable::kAnyNamed};
// mirror/class-inl.h, Android 11--13: throwing resolved method access and
// the exact retained-class chain used by Class::IsObsoleteVersionOf.
inline constexpr NamedSymbol kResolvedMethodAccess{Library::kArt, "_ZN3art6mirror5Class24ResolvedMethodAccessTestILb1EEEbNS_6ObjPtrIS1_EEPNS_9ArtMethodENS3_INS0_8DexCacheEEEjNS_10InvokeTypeE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kGetObsoleteClass{Library::kArt, "_ZN3art6mirror5Class16GetObsoleteClassEv", SymbolTable::kAnyNamed};
// Observer only until an exact direct-resolution boundary has been proved.
inline constexpr NamedSymbol kCanAccessMember{Library::kArt, "_ZN3art6mirror5Class15CanAccessMemberENS_6ObjPtrIS1_EEj", SymbolTable::kAnyNamed};
// nterp.cc. Android 14+ also uses this cache-fill entry in the switch interpreter.
inline constexpr NamedSymbol kNterpGetMethod{Library::kArt, "NterpGetMethod", SymbolTable::kAnyNamed};
// entrypoint_utils-inl.h, Android 14--17: switch interpreter invocation selection.
// Each instantiation may be inlined independently. Keep the available selector;
// all of 11--13 inline both, while some 14/17 modules retain interface only.
inline constexpr NamedSymbol kSelectVirtual{Library::kArt, "_ZN3art16FindMethodToCallILNS_10InvokeTypeE2EEEPNS_9ArtMethodEPNS_6ThreadES3_PNS_6ObjPtrINS_6mirror6ObjectEEERKNS_11InstructionEbPb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kSelectInterface{Library::kArt, "_ZN3art16FindMethodToCallILNS_10InvokeTypeE4EEEPNS_9ArtMethodEPNS_6ThreadES3_PNS_6ObjPtrINS_6mirror6ObjectEEERKNS_11InstructionEbPb", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodDescriptor{Library::kArt, "_ZN3art9ArtMethod27GetDeclaringClassDescriptorEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodLoader{Library::kArt, "_ZN3art9ArtMethod14GetClassLoaderEv", SymbolTable::kAnyNamed};
// art_method.cc: copied obsolete defaults live outside the declaring interface's array.
// Android 11--13 use the non-template read-barrier entry.
inline constexpr NamedSymbol kObsoleteCacheLegacy{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObsoleteCacheRead{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE0EEENS_6ObjPtrINS_6mirror8DexCacheEEEv", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kObsoleteCacheRaw{Library::kArt, "_ZN3art9ArtMethod19GetObsoleteDexCacheILNS_17ReadBarrierOptionE1EEENS_6ObjPtrINS_6mirror8DexCacheEEEv", SymbolTable::kAnyNamed};
// method_handles.cc: the post-adaptation invocation boundary (13--17).
inline constexpr NamedSymbol kHandleDispatch0{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kHandleDispatch1{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE.__uniq.220311785481449817352614677537007458746", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kHandleDispatch2{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE.__uniq.220311785481449817352614677537007458746.llvm.17222293888448412119", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kHandleDispatch3{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE.__uniq.220311785481449817352614677537007458746.llvm.3132849359635259364", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kHandleDispatch4{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE.__uniq.220311785481449817352614677537007458746.llvm.16360391822823227215", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kHandleDispatch5{Library::kArt, "_ZN3art12_GLOBAL__N_126DoMethodHandleInvokeMethodEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEEPKNS_19InstructionOperandsEPNS_6JValueE.__uniq.220311785481449817352614677537007458746.llvm.16692137122462350267", SymbolTable::kAnyNamed};
// Android 11--12 retain the refinement entry with caller-transformer support.
inline constexpr NamedSymbol kHandleRefineLegacy{Library::kArt, "_ZN3art12_GLOBAL__N_118RefineTargetMethodEPNS_6ThreadERNS_11ShadowFrameERKNS_6mirror12MethodHandle4KindENS_6HandleINS5_10MethodTypeEEESC_jPNS_9ArtMethodE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kClassDescriptorEquals{Library::kArt, "_ZN3art6mirror5Class16DescriptorEqualsEPKc", SymbolTable::kAnyNamed};
}  // namespace artdcevm::symbols::invoke_kind
