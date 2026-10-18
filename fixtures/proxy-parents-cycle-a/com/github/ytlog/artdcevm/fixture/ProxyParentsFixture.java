package com.github.ytlog.artdcevm.fixture;
public final class ProxyParentsFixture {
    public interface Shared { default int shared() { return 31; } }
    public interface Added { default int added() { return 41; } int missing(); }
    public interface A extends B { int a(); }
    public interface B extends Shared { int b(); }
}
