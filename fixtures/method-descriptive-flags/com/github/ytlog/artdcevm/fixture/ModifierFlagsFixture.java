package com.github.ytlog.artdcevm.fixture;

public final class ModifierFlagsFixture {
    public static class Subject {
        public int sum(int[] values) {
            int total = 1;
            for (int value : values) total += value;
            return total;
        }
        public double half(double value) { return value / 4; }
    }
}
