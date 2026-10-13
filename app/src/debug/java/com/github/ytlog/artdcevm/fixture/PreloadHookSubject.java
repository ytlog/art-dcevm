package com.github.ytlog.artdcevm.fixture;

/**
 * Redefined with its own bytes, which a ClassFileLoadHook replaces with
 * fixtures/preload-hook: only then does it name PreloadHookChild.
 */
public class PreloadHookSubject {
    public int own;
    public static int probe() { return 0; }
}
