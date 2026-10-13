package com.github.ytlog.artdcevm.fixture;

/**
 * Updated through the tool protocol (fixtures/tool-v1): its new version uses
 * classes the APK does not contain, which the tool appends to the loader.
 */
public class ToolHost {
    public int own = 6;
    public static int run() { return 0; }
}
