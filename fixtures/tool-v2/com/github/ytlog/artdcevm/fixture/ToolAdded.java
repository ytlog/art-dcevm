package com.github.ytlog.artdcevm.fixture;

/** Added by the tool (fixtures/tool-v1), then redefined like any loaded class (tool-v2). */
public class ToolAdded {
    public static int twice(int value) { return value * 2 + offset(); }
    static int offset() { return 1; }
}
