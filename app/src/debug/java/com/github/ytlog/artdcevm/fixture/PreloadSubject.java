package com.github.ytlog.artdcevm.fixture;

/**
 * Its redefinition (fixtures/preload-subclass) adds a field and first names
 * PreloadChild, which nothing has loaded before.
 */
public class PreloadSubject {
    public int own;
    public static int probe() { return 0; }
}
