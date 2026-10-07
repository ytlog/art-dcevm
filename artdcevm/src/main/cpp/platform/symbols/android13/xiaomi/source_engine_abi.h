#pragma once

// Source-compiled ART 13 aliases need string literals for __asm__ names.
// Keep these in the device profile alongside the symbols resolved by HookEngine.
// The first name belongs to the device's libopenjdkjvmti.so; the remaining
// names refer to our renamed SourceRedefiner implementation.
#define ARTDCEVM_A13_ORIGINAL_STRUCTURAL_REDEFINE \
  "_ZN12openjdkjvmti9Redefiner27StructurallyRedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition"
#define ARTDCEVM_A13_SOURCE_STRUCTURAL_COMMIT \
  "_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition23UpdateClassStructurallyERKNS_20RedefinitionDataIterE"
#define ARTDCEVM_A13_SOURCE_CHECK_METHODS \
  "_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition12CheckMethodsEv"
#define ARTDCEVM_A13_SOURCE_CHECK_CLASS \
  "_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition10CheckClassEv"
#define ARTDCEVM_A13_SOURCE_COLLECT_NEW_INSTANCES \
  "_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition28CollectAndCreateNewInstancesEPNS_20RedefinitionDataIterE"
#define ARTDCEVM_A13_SOURCE_ALLOCATE_SUBTREE \
  "_ZN12openjdkjvmti15SourceRedefiner17ClassRedefinition25FinishNewClassAllocationsERNS_22RedefinitionDataHolderEPNS_20RedefinitionDataIterE"
#define ARTDCEVM_A13_SOURCE_REDEFINE_RUN \
  "_ZN12openjdkjvmti15SourceRedefiner3RunEv"
