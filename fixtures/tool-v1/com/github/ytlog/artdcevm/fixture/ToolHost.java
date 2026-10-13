package com.github.ytlog.artdcevm.fixture;

import java.util.function.IntSupplier;

/**
 * Names three classes the APK lacks: ToolAdded, the subclass ToolChild and
 * the lambda class D8 synthesizes. The new field takes own's offset, so a
 * ToolChild left under the old layout would share own with its own field.
 */
public class ToolHost {
    public int extra;
    public int own = 6;
    public int describe() { return 1; }
    public static int run() {
        ToolHost host = new ToolChild();
        IntSupplier next = () -> host.own + 1;
        return ToolAdded.twice(next.getAsInt()) * 100 + host.describe();
    }
}
