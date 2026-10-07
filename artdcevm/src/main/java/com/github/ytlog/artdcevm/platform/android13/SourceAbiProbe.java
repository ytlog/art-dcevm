package com.github.ytlog.artdcevm.platform.android13;

/** SDK-owned calibration object. Its schema and initial values are a native ABI contract. */
final class SourceAbiProbe {
    public int value = 37;
    public static int counter = 7;
    public int compute() { return value + 1; }
}
