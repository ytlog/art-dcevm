#pragma once

// Source-compiled ART 13 aliases need string literals for __asm__ names.
// Keep these in the device profile alongside the symbols resolved by HookEngine.
// This name belongs to the device's libopenjdkjvmti.so.
#define ARTDCEVM_A13_ORIGINAL_STRUCTURAL_REDEFINE \
  "_ZN12openjdkjvmti9Redefiner27StructurallyRedefineClassesEP9_jvmtiEnviPK21_jvmtiClassDefinition"
