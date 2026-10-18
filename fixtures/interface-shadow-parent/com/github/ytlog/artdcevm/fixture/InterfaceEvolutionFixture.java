package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface ShadowParent { default int shadow() { return 7; } }
 public interface ProtectedShadow extends ShadowParent { default int stable() { return 0; }  static int call(ProtectedShadow value) { return value.shadow(); }
 static int hot(ProtectedShadow value, int count) {
   int failures = 0;
   for (int i = 0; i < count; ++i) try { value.shadow(); } catch (IllegalAccessError expected) { ++failures; }
   return failures;
 }
 }
}
