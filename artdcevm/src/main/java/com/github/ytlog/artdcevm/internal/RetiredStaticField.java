package com.github.ytlog.artdcevm.internal;

/**
 * Targets for reflective {@code Field} objects whose static field was retired.
 * Static access initializes the declaring class; this class always fails to
 * initialize, so such access throws instead of reading these placeholders.
 */
public final class RetiredStaticField {
    private static boolean retiredBoolean;
    private static byte retiredByte;
    private static char retiredChar;
    private static short retiredShort;
    private static int retiredInt;
    private static long retiredLong;
    private static float retiredFloat;
    private static double retiredDouble;
    private static Object retiredReference;

    static {
        retire();
    }

    private RetiredStaticField() {}

    private static void retire() {
        throw new NoSuchFieldError("field retired by structural redefinition");
    }
}
