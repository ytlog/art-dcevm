package dev.artdcevm.fixture;

public final class NarrowNewParentCaller {
    private NarrowNewParentCaller() {}

    public static int touch() { return 1; }

    public static int readFresh() {
        NarrowParent parent = new NarrowSubject();
        return parent.ancestor;
    }

    public static int invokeFresh() {
        NarrowParent parent = new NarrowSubject();
        return parent.inherited();
    }
}
