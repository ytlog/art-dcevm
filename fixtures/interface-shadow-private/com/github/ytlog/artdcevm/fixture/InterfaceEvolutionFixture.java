package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface PrivateShadow { default int stable() { return 0; } default int shadow() { return 1; } static int call(PrivateShadow value) { return value.shadow(); }
 static int hot(PrivateShadow value, int count) {
   int failures = 0;
   for (int i = 0; i < count; ++i) try { value.shadow(); } catch (IllegalAccessError expected) { ++failures; }
   return failures;
 }
 }
}
