package com.github.ytlog.artdcevm.fixture;

/** A superclass change submitted with its old or new superclass chain. */
public final class SuperChainFixture {
    private SuperChainFixture() {}

    public static class Base {
        public int baseValue = 5;
        public int base() { return baseValue + 1; }
    }
    public static class Middle extends Base {
        public int middleValue = 11;
        public int middle() { return 50 + middleValue; }
    }
    public static class Subject extends Middle {
        public int own = 7;
        public int chain() { return middle() + base() + own; }
    }
}
