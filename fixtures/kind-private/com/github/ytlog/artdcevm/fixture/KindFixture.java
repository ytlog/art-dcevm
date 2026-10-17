package com.github.ytlog.artdcevm.fixture;

public final class KindFixture {
    public static class Subject {
        public int marker = 3;
        public int convert(int value) { return value + 2; }
        private int helper() { return 6; }
        public int useHelper() { return helper(); }
    }
}
