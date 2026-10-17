package com.github.ytlog.artdcevm.fixture;

public final class KindFixture {
    public static class Subject {
        public int marker = 3;
        public int convert(int value) { return value + 2; }
        public int helper() { return 5; }
        public int useHelper() { return helper(); }
    }
    public static class Caller {
        public static int call() { return new Subject().convert(1); }
    }
}
