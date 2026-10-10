#pragma once
#include <string>
#include "platform/abi/arm64/android11/async_deopt.h"
// Fixed named ART/JVMTI declarations for the Android13 source family.
// Symbol presence alone does not establish private ABI or runtime permission.
namespace artdcevm::symbols::android13 {
inline constexpr char kRedefineClasses[] = "_ZN12openjdkjvmti9Redefiner15RedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition";
inline constexpr char kStructuralRedefineClasses[] = "_ZN12openjdkjvmti9Redefiner27StructurallyRedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition";
inline constexpr char kIsStructurallyModifiableClass[] = "_ZN12openjdkjvmti9Redefiner17IsModifiableClassEP9_jvmtiEnvP7_jclassPh";
inline constexpr char kHGraphInlineInto[] = "_ZN3art6HGraph10InlineIntoEPS0_PNS_7HInvokeE";
inline constexpr char kCheckFields[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition11CheckFieldsEv";
inline constexpr char kRecordFailure[] = "_ZN12openjdkjvmti9Redefiner13RecordFailureE10jvmtiErrorRKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEESA_";
inline constexpr char kGetCanonicalMethod[] = "_ZN3art9ArtMethod18GetCanonicalMethodENS_11PointerSizeE";
inline constexpr char kGetMirrorClass[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv";
inline constexpr char kRecordNewFieldAdded[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition19RecordNewFieldAddedEv";
inline constexpr char kCheckMethods[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition12CheckMethodsEv";
inline constexpr char kCheckClass[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition10CheckClassEv";
inline constexpr char kCollectNewInstances[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE";
inline constexpr char kAllocateSubtree[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE";
inline constexpr char kRedefineRun[] = "_ZN12openjdkjvmti9Redefiner3RunEv";
inline constexpr char kAllocateClass[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition22AllocateNewClassObjectEN3art6HandleINS2_6mirror5ClassEEES6_NS3_INS4_8DexCacheEEEt";
inline constexpr char kVerifyClass[] = "_ZN3art8verifier13ClassVerifier11VerifyClassEPNS_6ThreadEPNS0_12VerifierDepsEPKNS_7DexFileENS_6HandleINS_6mirror5ClassEEENS9_INSA_8DexCacheEEENS9_INSA_11ClassLoaderEEERKNS_3dex8ClassDefEPNS_17CompilerCallbacksENS0_15HardFailLogModeEjPNSt3__112basic_stringIcNSO_11char_traitsIcEENSO_9allocatorIcEEEE";
inline constexpr char kVerifierResolve[] = "_ZN3art8verifier12RegTypeCache12ResolveClassEPKcNS_6ObjPtrINS_6mirror11ClassLoaderEEE";
inline constexpr char kFindClass[] = "_ZN3art11ClassLinker9FindClassEPNS_6ThreadEPKcNS_6HandleINS_6mirror11ClassLoaderEEE";
inline constexpr char kLookupClass[] = "_ZN3art11ClassLinker11LookupClassEPNS_6ThreadEPKcNS_6ObjPtrINS_6mirror11ClassLoaderEEE";
inline constexpr char kDeleteClassLoader[] = "_ZN3art11ClassLinker17DeleteClassLoaderEPNS_6ThreadERKNS0_15ClassLoaderDataEb";
inline constexpr char kLinearAllocContainsUnsafe[] = "_ZNK3art11LinearAlloc14ContainsUnsafeEPv";
inline constexpr auto& kIsAsyncDeoptimizeable = ::artdcevm::abi::arm64::android11::kIsAsyncDeoptimizeable;
inline constexpr char kTestSuspendFromCode[] = "artTestSuspendFromCode";
inline constexpr char kPushDeoptimizationContext[] = "_ZN3art6Thread25PushDeoptimizationContextERKNS_6JValueEbNS_6ObjPtrINS_6mirror9ThrowableEEEbNS_24DeoptimizationMethodTypeE";
inline constexpr char kDeoptimize[] = "artDeoptimize";
inline constexpr char kReadBarrierMark[] = "artReadBarrierMark";
inline constexpr char kInvalidateAllCompiledCode[] = "_ZN3art3jit12JitCodeCache25InvalidateAllCompiledCodeEv";
inline constexpr char kCollectMappings[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition32CollectNewFieldAndMethodMappingsERKNS_20RedefinitionDataIterEPNSt3__13mapIPN3art9ArtMethodES9_NS5_4lessIS9_EENS5_9allocatorINS5_4pairIKS9_S9_EEEEEEPNS6_IPNS7_8ArtFieldESK_NSA_ISK_EENSC_INSD_IKSK_SK_EEEEEE";
inline constexpr char kStructuralCommit[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE";
inline constexpr char kInPlaceCommit[] = "_ZN12openjdkjvmti9Redefiner17ClassRedefinition18UpdateClassInPlaceERKNS_20RedefinitionDataIterE";
inline constexpr char kCheckedResolve[] = "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE";
inline constexpr char kCheckedResolveHandles[] = "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE1EEEPNS_9ArtMethodEjNS_6HandleINS_6mirror8DexCacheEEENS5_INS6_11ClassLoaderEEES4_NS_10InvokeTypeE";
inline constexpr const char* kRetiredMethodHooks[] = {
    "_ZN3art11interpreter30EnterInterpreterFromDeoptimizeEPNS_6ThreadEPNS_11ShadowFrameEPNS_6JValueEbNS_24DeoptimizationMethodTypeE",
    "NterpGetMethod",
    "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEPNS_6ThreadEjS4_NS_10InvokeTypeE",
    "_ZN3art11ClassLinker13ResolveMethodILNS0_11ResolveModeE0EEEPNS_9ArtMethodEjNS_6HandleINS_6mirror8DexCacheEEENS5_INS6_11ClassLoaderEEES4_NS_10InvokeTypeE",
    "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc",
    "_ZN3art35InvokeVirtualOrInterfaceWithJValuesIPNS_9ArtMethodEEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue",
    "_ZN3art35InvokeVirtualOrInterfaceWithVarArgsIPNS_9ArtMethodEEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list",
    "_ZN3art35InvokeVirtualOrInterfaceWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue",
    "_ZN3art35InvokeVirtualOrInterfaceWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list",
    "_ZN3art17InvokeWithJValuesIPNS_9ArtMethodEEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue",
    "_ZN3art17InvokeWithVarArgsIPNS_9ArtMethodEEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list",
    "_ZN3art17InvokeWithJValuesIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_PK6jvalue",
    "_ZN3art17InvokeWithVarArgsIP10_jmethodIDEENS_6JValueERKNS_33ScopedObjectAccessAlreadyRunnableEP8_jobjectT_St9__va_list",
    "_ZN3art12InvokeMethodILNS_11PointerSizeE8EEEP8_jobjectRKNS_33ScopedObjectAccessAlreadyRunnableES3_S3_S3_m",
    "_ZN3art18MethodHandleInvokeEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE",
    "_ZN3art23MethodHandleInvokeExactEPNS_6ThreadERNS_11ShadowFrameENS_6HandleINS_6mirror12MethodHandleEEENS4_INS5_10MethodTypeEEEPKNS_19InstructionOperandsEPNS_6JValueE",
    "_ZN3art32MethodHandleInvokeExactWithFrameEPNS_6ThreadENS_6HandleINS_6mirror12MethodHandleEEENS2_INS3_18EmulatedStackFrameEEE",
    "artInvokeObsoleteMethod",
    "_ZN3art12_GLOBAL__N_111ScopedCheck17CheckMethodAndSigERNS_18ScopedObjectAccessEP8_jobjectP7_jclassP10_jmethodIDNS_9Primitive4TypeENS_10InvokeTypeE",
    "_ZN3art12_GLOBAL__N_111ScopedCheck16CheckConstructorEP10_jmethodID",
    "_ZN3artL24Constructor_newInstance0EP7_JNIEnvP8_jobjectP13_jobjectArray",
    "_ZN3art3JNIILb0EE10NewObjectAEP7_JNIEnvP7_jclassP10_jmethodIDPK6jvalue",
    "_ZN3art3JNIILb1EE10NewObjectAEP7_JNIEnvP7_jclassP10_jmethodIDPK6jvalue",
    "_ZN3art3JNIILb0EE10NewObjectVEP7_JNIEnvP7_jclassP10_jmethodIDSt9__va_list",
    "_ZN3art3JNIILb1EE10NewObjectVEP7_JNIEnvP7_jclassP10_jmethodIDSt9__va_list",
};
inline constexpr const char* kRetiredFieldHooks[] = {
    "_ZN3art6mirror5Field11GetArtFieldEv",
    "_ZN3art6mirror5Field11VisitTargetEPNS_22ReflectiveValueVisitorE",
    "_ZN3art6mirror14FieldVarHandle6AccessENS0_9VarHandle10AccessModeEPNS_11ShadowFrameEPKNS_19InstructionOperandsEPNS_6JValueE",
    "_ZN3art12_GLOBAL__N_120MethodHandleFieldPutEPNS_6ThreadERNS_11ShadowFrameERNS_6ObjPtrINS_6mirror6ObjectEEEPNS_8ArtFieldENS_9Primitive4TypeERNS_6JValueE",
    "_ZN3art12_GLOBAL__N_111ScopedCheck16CheckFieldAccessERNS_18ScopedObjectAccessEP8_jobjectP9_jfieldIDbNS_9Primitive4TypeE",
    "NterpGetInstanceFieldOffset",
    "NterpGetStaticField",
};

inline std::string JniFieldSymbol(int indexed, bool is_static, bool setter,
                                  const char* type, const char* code, bool object_type) {
  std::string base = "_ZN3art3JNIILb" + std::to_string(indexed) + "EE";
  std::string receiver = is_static ? "EP7_JNIEnvP7_jclassP9_jfieldID"
                                   : "EP7_JNIEnvP8_jobjectP9_jfieldID";
  std::string name = std::string(setter ? "Set" : "Get") +
      (is_static ? "Static" : "") + type + "Field";
  std::string argument = object_type ? (is_static ? "P8_jobject" : "S5_") : code;
  return base + std::to_string(name.size()) + name + receiver + (setter ? argument : "");
}

inline std::string ReflectionFieldSymbol(bool setter, const char* type,
                                         const char* code, bool object_type) {
  std::string name = std::string(setter ? "Field_set" : "Field_get") +
      (object_type ? "" : type);
  std::string argument = object_type ? "S3_" : code;
  return "_ZN3artL" + std::to_string(name.size()) + name +
      "EP7_JNIEnvP8_jobjectS3_" + (setter ? argument : "");
}
}
