package com.github.ytlog.artdcevm.fixture;

/**
 * Supplied by a ClassFileLoadHook. Verifying it resolves PreloadHookChild; the
 * new field moves own into the slot PreloadHookChild's own field occupies
 * unless ART rebuilds it (see fixtures/preload-subclass).
 */
public class PreloadHookSubject {
    public int added;
    public int own;
    public static int probe() {
        PreloadHookChild child = new PreloadHookChild();
        child.added = 4;
        child.own = 3;
        child.child = 5;
        return child.added * 100 + child.own * 10 + child.child;
    }
}
