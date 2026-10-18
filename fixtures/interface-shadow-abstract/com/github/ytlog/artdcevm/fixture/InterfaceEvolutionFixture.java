package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface ProtectedShadow { default int stable() { return 0; } int shadow(); static int call(ProtectedShadow value) { return value.shadow(); }
 static int hot(ProtectedShadow value, int count) {
   int failures = 0;
   for (int i = 0; i < count; ++i) try { value.shadow(); } catch (IllegalAccessError expected) { ++failures; }
   return failures;
 }
 }
}
