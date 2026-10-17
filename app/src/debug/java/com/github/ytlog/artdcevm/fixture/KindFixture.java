package com.github.ytlog.artdcevm.fixture;

/** Methods whose invoke kind changes, and a separate loaded caller. */
public final class KindFixture {
    public static class Subject {
        public int marker = 3;
        public static int convert(int value) { return value + 1; }
        public int helper() { return 5; }
        public int useHelper() { return helper(); }
    }
    public static class Caller {
        public static int call() { return Subject.convert(1); }
    }
}
