package com.github.ytlog.artdcevm.internal;

/** Private one-shot constructor dispatch oracle; its constructor is retired by calibration. */
final class InitializerGuardOracle {
    public static int constructorCalls;
    public int intValue;

    public InitializerGuardOracle() {
        intValue = 0x81234567;
        constructorCalls++;
    }
}
