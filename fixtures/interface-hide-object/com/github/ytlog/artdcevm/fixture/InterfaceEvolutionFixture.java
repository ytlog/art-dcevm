package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface Left { default int left() { return 12; } default int conflict() { return 22; } }
 public interface HiddenParent { default int clash() { return 31; } }
 public interface Hiding extends HiddenParent { default int clash() { return 32; } }
 public interface ObjectHiding { default int stable() { return 41; } default int hashCodf() { return 42; } }
}
