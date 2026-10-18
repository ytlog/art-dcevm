package com.github.ytlog.artdcevm.fixture;

/** A superclass change submitted with its old or new superclass chain. */
public final class SuperChainFixture {
    private SuperChainFixture() {}

    public static class Base {
        public int baseValue = 5;
        public int base() { return baseValue; }
        public int added() { return 3; }
    }
    public static class Middle extends Base {
        public int middleValue = 11;
        public int extra;
        public int middle() { return 40 + middleValue; }
    }
    public static class Subject extends Middle {
        public int own = 7;
    }
}
