package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
    public interface StaticShadow { default int stable() { return 51; } }
    public interface PrivateShadow { default int stable() { return 61; } }
    public interface ProtectedShadow { default int stable() { return 71; } }
    public interface PackageShadow { default int stable() { return 81; } }
    public interface OtherPackageShadow { default int stable() { return 91; } }
}
