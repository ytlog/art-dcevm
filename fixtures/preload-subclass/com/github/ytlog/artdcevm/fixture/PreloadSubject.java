package com.github.ytlog.artdcevm.fixture;

/**
 * Verifying this definition resolves PreloadChild. Fields of one size are laid
 * out by name, so the new field takes own's offset and moves own to the next
 * slot: the one PreloadChild's own field occupies unless ART rebuilds it.
 */
public class PreloadSubject {
    public int added;
    public int own;
    public static int probe() {
        PreloadChild child = new PreloadChild();
        child.added = 4;
        child.own = 3;
        child.child = 5;
        return child.added * 100 + child.own * 10 + child.child;
    }
}
