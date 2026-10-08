package com.github.ytlog.artdcevm.testing;
/** Isolated real ART identities permanently retired only in the debug process. */
public final class RetiredEntryFixture {
    public int removedField=41;
    public int keptField=73;
    public static long removedStatic=13;
    public static long keptStatic=17;
    public static int removed() { return 41; }
    public static int kept() { return 73; }
}
