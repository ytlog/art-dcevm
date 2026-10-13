package com.github.ytlog.artdcevm.fixture;

/** Part of the APK but never loaded before the tool updates it; tests must not name it. */
public class ToolDormant {
    public static int value() { return helper() + 1; }
    static int helper() { return 1; }
}
