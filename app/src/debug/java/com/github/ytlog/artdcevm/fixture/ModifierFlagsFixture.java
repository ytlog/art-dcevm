package com.github.ytlog.artdcevm.fixture;

/** Method flags that only describe a method, and interface class modifiers. */
public final class ModifierFlagsFixture {
    public static class Subject {
        public int sum(int... values) {
            int total = 0;
            for (int value : values) total += value;
            return total;
        }
        public strictfp double half(double value) { return value / 2; }
    }
    public interface Visible {
        default int value() { return 1; }
    }
    public static class Implementation implements Visible {
        public int marker = 3;
    }
}
