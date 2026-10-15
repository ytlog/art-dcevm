package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface PrivateShadow { default int stable() { return 0; } default int shadow() { return 1; } }
}
