package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface OtherPackageShadow { default int stable() { return 0; } default int shadow() { return 1; } }
}
