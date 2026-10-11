#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Named ART structural-redefinition decision points and services. The mangled
// names are identical in every collected Android 11--17 arm64 runtime and all
// live in .dynsym; tools/check-structural-symbols.py re-verifies this catalog
// against assets/runtime_lib. Keep one declaration per line in this exact form,
// the tool parses it.
namespace artdcevm::symbols::structural {

// Entry routing.
inline constexpr NamedSymbol kRedefineClasses{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner15RedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition", HookRoute::kInline};
inline constexpr NamedSymbol kStructurallyRedefineClasses{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner27StructurallyRedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition", HookRoute::kInline};

// Validation policy.
inline constexpr NamedSymbol kCheckClass{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition10CheckClassEv", HookRoute::kInline};
inline constexpr NamedSymbol kCheckFields{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition11CheckFieldsEv", HookRoute::kInline};
inline constexpr NamedSymbol kCheckMethods{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition12CheckMethodsEv", HookRoute::kInline};
inline constexpr NamedSymbol kRecordNewFieldAdded{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition19RecordNewFieldAddedEv", HookRoute::kInline};
inline constexpr NamedSymbol kRecordNewMethodAdded{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition20RecordNewMethodAddedEv", HookRoute::kInline};

// Member mapping, class allocation and structural commit.
inline constexpr NamedSymbol kCollectNewFieldAndMethodMappings{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition32CollectNewFieldAndMethodMappingsERKNS_20RedefinitionDataIterEPNSt3__13mapIPN3art9ArtMethodES9_NS5_4lessIS9_EENS5_9allocatorINS5_4pairIKS9_S9_EEEEEEPNS6_IPNS7_8ArtFieldESK_NSA_ISK_EENSC_INSD_IKSK_SK_EEEEEE", HookRoute::kInline};
inline constexpr NamedSymbol kAllocateNewClassObject{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition22AllocateNewClassObjectEN3art6HandleINS2_6mirror5ClassEEES6_NS3_INS4_8DexCacheEEEt", HookRoute::kInline};
inline constexpr NamedSymbol kFinishNewClassAllocations{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE", HookRoute::kInline};
inline constexpr NamedSymbol kCollectAndCreateNewInstances{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE", HookRoute::kInline};
inline constexpr NamedSymbol kUpdateClassStructurally{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE", HookRoute::kInline};
inline constexpr NamedSymbol kReplaceReferences{Library::kJvmti, "_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE", HookRoute::kInline};

// Runtime services used by the owned commit and retired-member checks.
inline constexpr NamedSymbol kVisitReflectiveTargets{Library::kArt, "_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE", HookRoute::kInline};
inline constexpr NamedSymbol kDeoptimizeAllThreadFrames{Library::kArt, "_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv", HookRoute::kInline};
inline constexpr NamedSymbol kDecodeFieldId{Library::kArt, "_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID", HookRoute::kInline};
inline constexpr NamedSymbol kDecodeMethodId{Library::kArt, "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID", HookRoute::kInline};

}  // namespace artdcevm::symbols::structural
