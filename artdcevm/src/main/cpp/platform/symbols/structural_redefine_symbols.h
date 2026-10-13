#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Named ART symbols the engine resolves on every runtime. The mangled names are
// identical in every collected Android 11--17 arm64 runtime; 21 entries require
// .dynsym and 3 allow any named table. Presence proves the name only, not the
// source declarations or runtime behavior. tools/check-structural-symbols.py
// re-verifies this catalog against assets/runtime_lib. Keep one declaration per
// line in this exact form, the tool parses it.
namespace artdcevm::symbols::structural {

// Entry routing.
// OpenjdkJvmTi.cc places this wrapper in the public JVMTI function table.
// Optimized wrappers can inline the exported Redefiner::RedefineClasses entry.
inline constexpr NamedSymbol kJvmtiRedefineClasses{Library::kJvmti, "_ZN12openjdkjvmti14JvmtiFunctions15RedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kStructurallyRedefineClasses{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner27StructurallyRedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition"};

// Validation policy.
inline constexpr NamedSymbol kCheckClass{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition10CheckClassEv"};
inline constexpr NamedSymbol kCheckFields{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition11CheckFieldsEv"};
inline constexpr NamedSymbol kCheckMethods{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition12CheckMethodsEv"};
inline constexpr NamedSymbol kRecordNewFieldAdded{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition19RecordNewFieldAddedEv"};
inline constexpr NamedSymbol kRecordNewMethodAdded{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv"};
inline constexpr NamedSymbol kClassRedefinitionGetMirrorClass{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv"};
// Verifies the new DEX against the new class object; a superclass change
// redirects the verifier's lookup of the class's own descriptor meanwhile.
inline constexpr NamedSymbol kCheckVerification{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition17CheckVerificationERKNS_20RedefinitionDataIterE"};

// Member mapping, class allocation and the structural commit, all run by ART.
inline constexpr NamedSymbol kCollectNewFieldAndMethodMappings{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition32CollectNewFieldAndMethodMappingsERKNS_20RedefinitionDataIterEPNSt3__13mapIPN3art9ArtMethodES9_NS5_4lessIS9_EENS5_9allocatorINS5_4pairIKS9_S9_EEEEEEPNS6_IPNS7_8ArtFieldESK_NSA_ISK_EENSC_INSD_IKSK_SK_EEEEEE"};
inline constexpr NamedSymbol kAllocateNewClassObject{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition22AllocateNewClassObjectEN3art6HandleINS2_6mirror5ClassEEES6_NS3_INS4_8DexCacheEEEt"};
inline constexpr NamedSymbol kCollectAndCreateNewInstances{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE"};
inline constexpr NamedSymbol kUpdateClassStructurally{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE"};

// Runtime services for layout probes and retired-member checks.
inline constexpr NamedSymbol kVisitReflectiveTargets{Library::kArt, "_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE"};
inline constexpr NamedSymbol kFindDeclaredStaticField{Library::kArt, "_ZN3art6mirror5Class23FindDeclaredStaticFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kFindDeclaredInstanceField{Library::kArt, "_ZN3art6mirror5Class25FindDeclaredInstanceFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kFindInstanceField{Library::kArt, "_ZN3art6mirror5Class17FindInstanceFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kHasSameNameAndSignature{Library::kArt, "_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"};
inline constexpr NamedSymbol kGetCanonicalMethod{Library::kArt, "_ZN3art9ArtMethod18GetCanonicalMethodENS_11PointerSizeE"};
inline constexpr NamedSymbol kEncodeMethodId{Library::kArt, "_ZN3art3jni12JniIdManager14EncodeMethodIdEPNS_9ArtMethodE"};
inline constexpr NamedSymbol kEncodeMethodIdHandle{Library::kArt, "_ZN3art3jni12JniIdManager14EncodeMethodIdENS_16ReflectiveHandleINS_9ArtMethodEEE"};
inline constexpr NamedSymbol kThrowNewException{Library::kArt, "_ZN3art6Thread17ThrowNewExceptionEPKcS2_"};
inline constexpr NamedSymbol kMethodHandleInvoke{Library::kArt, "_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodHandleInvokeExact{Library::kArt, "_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE", SymbolTable::kAnyNamed};

}  // namespace artdcevm::symbols::structural
