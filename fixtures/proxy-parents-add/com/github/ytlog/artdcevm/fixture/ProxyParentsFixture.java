package com.github.ytlog.artdcevm.fixture;
public final class ProxyParentsFixture {
    public interface Shared { default int shared() { return 31; } }
    public interface Added { default int added() { return 43; } int missing(); default int extra() { return 44; } }
    public interface A extends Shared, Added { int a(); }
    public interface B extends Shared, Added { int b(); }
}
