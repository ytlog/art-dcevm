#pragma once
#include "platform/symbols/art_symbol_compat.h"

// Named ART structural-redefinition decision points and services. The mangled
// names are identical in every collected Android 11--17 arm64 runtime.
// 56 entries require .dynsym; 10 allow any named table, including the public
// wrapper, CopyFields candidate and method-access guards. Each declares its scope.
// This catalog proves exact-name presence, not source ABI or Phase 3/4 behavior.
// tools/check-structural-symbols.py re-verifies this catalog
// against assets/runtime_lib. Keep one declaration per line in this exact form,
// the tool parses it.
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

// Member mapping, class allocation and structural commit.
inline constexpr NamedSymbol kCollectNewFieldAndMethodMappings{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition32CollectNewFieldAndMethodMappingsERKNS_20RedefinitionDataIterEPNSt3__13mapIPN3art9ArtMethodES9_NS5_4lessIS9_EENS5_9allocatorINS5_4pairIKS9_S9_EEEEEEPNS6_IPNS7_8ArtFieldESK_NSA_ISK_EENSC_INSD_IKSK_SK_EEEEEE"};
inline constexpr NamedSymbol kAllocateNewClassObject{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition22AllocateNewClassObjectEN3art6HandleINS2_6mirror5ClassEEES6_NS3_INS4_8DexCacheEEEt"};
inline constexpr NamedSymbol kFinishNewClassAllocations{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE"};
inline constexpr NamedSymbol kCollectAndCreateNewInstances{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE"};
inline constexpr NamedSymbol kUpdateClassStructurally{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE"};
inline constexpr NamedSymbol kReplaceReferences{Library::kJvmti, "_ZN12openjdkjvmti14HeapExtensions17ReplaceReferencesEPN3art6ThreadERKNSt3__113unordered_mapINS1_6ObjPtrINS1_6mirror6ObjectEEES9_NS1_10HashObjPtrENS4_8equal_toIS9_EENS4_9allocatorINS4_4pairIKS9_S9_EEEEEE"};

// Runtime services used by the owned commit and retired-member checks.
inline constexpr NamedSymbol kVisitReflectiveTargets{Library::kArt, "_ZN3art7Runtime22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE"};
inline constexpr NamedSymbol kDeoptimizeAllThreadFrames{Library::kArt, "_ZN3art15instrumentation15Instrumentation25DeoptimizeAllThreadFramesEv"};
inline constexpr NamedSymbol kDecodeFieldId{Library::kArt, "_ZN3art3jni12JniIdManager13DecodeFieldIdEP9_jfieldID"};
inline constexpr NamedSymbol kDecodeMethodId{Library::kArt, "_ZN3art3jni12JniIdManager14DecodeMethodIdEP10_jmethodID"};

// Additional services audited for the owned commit/allocation plan. These are
// declarations only; Phase 1/2 installation does not resolve or call them.
// Version-specific and incomplete candidates stay in tools/structural-symbol-audit.json.
// Keep the ART transaction, GC replacement, deoptimization and cache algorithms.
inline constexpr NamedSymbol kInvalidateAllCompiledCode{Library::kArt, "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv"};
inline constexpr NamedSymbol kThreadListForEach{Library::kArt, "_ZN3art10ThreadList7ForEachEPFvPNS_6ThreadEPvES3_"};
inline constexpr NamedSymbol kInterpreterCacheClear{Library::kArt, "_ZN3art16InterpreterCache5ClearEPNS_6ThreadE"};
inline constexpr NamedSymbol kSetEntryPointsForObsoleteMethod{Library::kArt, "_ZNK3art11ClassLinker31SetEntryPointsForObsoleteMethodEPNS_9ArtMethodE"};
inline constexpr NamedSymbol kSetObsoleteClass{Library::kArt, "_ZN3art6mirror8ClassExt16SetObsoleteClassENS_6ObjPtrINS0_5ClassEEE"};
inline constexpr NamedSymbol kSetStatusLocked{Library::kArt, "_ZN3art6mirror5Class15SetStatusLockedENS_11ClassStatusE"};
inline constexpr NamedSymbol kSetStatus{Library::kArt, "_ZN3art6mirror5Class9SetStatusENS_6HandleIS1_EENS_11ClassStatusEPNS_6ThreadE"};
inline constexpr NamedSymbol kAllocClass{Library::kArt, "_ZN3art11ClassLinker10AllocClassEPNS_6ThreadEj"};
inline constexpr NamedSymbol kSetupClass{Library::kArt, "_ZN3art11ClassLinker10SetupClassERKNS_7DexFileERKNS_3dex8ClassDefENS_6HandleINS_6mirror5ClassEEENS_6ObjPtrINS9_11ClassLoaderEEE"};
inline constexpr NamedSymbol kLoadClass{Library::kArt, "_ZN3art11ClassLinker9LoadClassEPNS_6ThreadERKNS_7DexFileERKNS_3dex8ClassDefENS_6HandleINS_6mirror5ClassEEE"};
inline constexpr NamedSymbol kLinkClass{Library::kArt, "_ZN3art11ClassLinker9LinkClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror5ClassEEENS5_INS6_11ObjectArrayIS7_EEEEPNS_13MutableHandleIS7_EE"};
inline constexpr NamedSymbol kForceClassInitialized{Library::kArt, "_ZN3art11ClassLinker21ForceClassInitializedEPNS_6ThreadENS_6HandleINS_6mirror5ClassEEE"};
inline constexpr NamedSymbol kEnsureInstanceFieldIds{Library::kArt, "_ZN3art6mirror5Class22EnsureInstanceFieldIdsENS_6HandleIS1_EE"};
inline constexpr NamedSymbol kEnsureStaticFieldIds{Library::kArt, "_ZN3art6mirror5Class20EnsureStaticFieldIdsENS_6HandleIS1_EE"};
inline constexpr NamedSymbol kEnsureMethodIds{Library::kArt, "_ZN3art6mirror5Class15EnsureMethodIdsENS_6HandleIS1_EE"};
inline constexpr NamedSymbol kGetNewClassSize{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition15GetNewClassSizeERN3art13ClassAccessorE"};
inline constexpr NamedSymbol kFindDeclaredStaticField{Library::kArt, "_ZN3art6mirror5Class23FindDeclaredStaticFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kFindDeclaredInstanceField{Library::kArt, "_ZN3art6mirror5Class25FindDeclaredInstanceFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kFindInstanceField{Library::kArt, "_ZN3art6mirror5Class17FindInstanceFieldENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEES6_"};
inline constexpr NamedSymbol kFindClassMethod{Library::kArt, "_ZN3art6mirror5Class15FindClassMethodENSt3__117basic_string_viewIcNS2_11char_traitsIcEEEERKNS_9SignatureENS_11PointerSizeE"};
inline constexpr NamedSymbol kHasSameNameAndSignature{Library::kArt, "_ZN3art9ArtMethod23HasSameNameAndSignatureEPS0_"};
inline constexpr NamedSymbol kSetNotIntrinsic{Library::kArt, "_ZN3art9ArtMethod15SetNotIntrinsicEv"};
inline constexpr NamedSymbol kSetDexCache{Library::kArt, "_ZN3art6mirror5Class11SetDexCacheENS_6ObjPtrINS0_8DexCacheEEE"};
// ExclusiveLock is intentionally absent: one OEM has an unverified extra argument.
// Unlock/ForEach/Clear presence does not resolve that locking ABI gap.
inline constexpr NamedSymbol kMutexExclusiveUnlock{Library::kArt, "_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"};
inline constexpr NamedSymbol kThreadListLock{Library::kArt, "_ZN3art5Locks17thread_list_lock_E"};
inline constexpr NamedSymbol kRuntimeInstance{Library::kArt, "_ZN3art7Runtime9instance_E"};
inline constexpr NamedSymbol kCompareClasses{Library::kJvmti, "_ZN12openjdkjvmti14CompareClassesEN3art6ObjPtrINS0_6mirror5ClassEEES4_"};
// Slow resolution may load classes; use before class-loading pause, with fixed ABI.
inline constexpr NamedSymbol kDoResolveType{Library::kArt, "_ZN3art11ClassLinker13DoResolveTypeENS_3dex9TypeIndexENS_6HandleINS_6mirror8DexCacheEEENS3_INS4_11ClassLoaderEEE"};
inline constexpr NamedSymbol kObjectLockClassConstructor{Library::kArt, "_ZN3art10ObjectLockINS_6mirror5ClassEEC1EPNS_6ThreadENS_6HandleIS2_EE"};
inline constexpr NamedSymbol kObjectLockClassDestructor{Library::kArt, "_ZN3art10ObjectLockINS_6mirror5ClassEED1Ev"};

// Audited local decision candidate, not an installed Hook. Recursive source does
// not prove every optimized top-level copy reaches this symbol; test call coverage
// before preferring this narrower hook over an owned UpdateClassStructurally.
inline constexpr NamedSymbol kCopyFields{Library::kJvmti, "_ZN12openjdkjvmtiL10CopyFieldsEbN3art6ObjPtrINS0_6mirror6ObjectEEENS1_INS2_5ClassEEES4_S6_", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kRedefinerRecordFailure{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner13RecordFailureE10jvmtiErrorRKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEESA_"};
inline constexpr NamedSymbol kAssertPendingOOMException{Library::kArt, "_ZNK3art6Thread25AssertPendingOOMExceptionEv"};

// Method-retirement guards. These are exact names present in all 27 samples;
// several inline/template entry helpers require the named local table.
inline constexpr NamedSymbol kArtMethodInvoke{Library::kArt, "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc"};
inline constexpr NamedSymbol kFromReflectedMethod{Library::kArt, "_ZN3art9ArtMethod19FromReflectedMethodERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobject"};
inline constexpr NamedSymbol kThrowNewException{Library::kArt, "_ZN3art6Thread17ThrowNewExceptionEPKcS2_"};
inline constexpr NamedSymbol kThrowNewWrappedException{Library::kArt, "_ZN3art6Thread24ThrowNewWrappedExceptionEPKcS2_"};
inline constexpr NamedSymbol kInvokeObsoleteMethod{Library::kArt, "artInvokeObsoleteMethod", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kDeliverPendingException{Library::kArt, "artDeliverPendingExceptionFromCode", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodHandleInvoke{Library::kArt, "_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kMethodHandleInvokeExact{Library::kArt, "_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInvokeWithJValuesId{Library::kArt, "_ZN3art17InvokeWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInvokeWithVarArgsId{Library::kArt, "_ZN3art17InvokeWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInvokeVirtualWithJValuesId{Library::kArt, "_ZN3art35InvokeVirtualOrInterfaceWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kInvokeVirtualWithVarArgsId{Library::kArt, "_ZN3art35InvokeVirtualOrInterfaceWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list", SymbolTable::kAnyNamed};
inline constexpr NamedSymbol kEncodeMethodId{Library::kArt, "_ZN3art3jni12JniIdManager14EncodeMethodIdEPNS_9ArtMethodE"};
inline constexpr NamedSymbol kEncodeMethodIdHandle{Library::kArt, "_ZN3art3jni12JniIdManager14EncodeMethodIdENS_16ReflectiveHandleINS_9ArtMethodEEE"};
inline constexpr NamedSymbol kGetCanonicalMethod{Library::kArt, "_ZN3art9ArtMethod18GetCanonicalMethodENS_11PointerSizeE"};
inline constexpr NamedSymbol kClassRedefinitionGetMirrorClass{Library::kJvmti, "_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv"};

}  // namespace artdcevm::symbols::structural
