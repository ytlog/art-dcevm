package dev.artdcevm.fixture;

public final class NarrowSuperHotCaller {
    private NarrowSuperHotCaller() {}
    public static int call(NarrowChild child) { return child.callInheritedSuper(); }
}
