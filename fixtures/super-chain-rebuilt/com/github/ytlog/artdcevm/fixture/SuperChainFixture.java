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
        public int extra;
        public int middle() { return 50 + middleValue; }
        public int extraValue() { return extra + 100; }
    }
    public static class Subject extends Middle {
        public int own = 7;
        public int chain() { return middle() + base() + own; }
    }
    public static class Deep extends Middle {
        public int deepValue = 13;
        public int deep() { return deepValue; }
    }
    public static class Mover extends Middle {
        public int moverValue = 23;
        public int viaMiddle() { return extraValue() + middle() + moverValue; }
    }
    public static class DeepMover extends Deep {
        public int value = 29;
        public int viaDeep() { return deep() + extraValue() + value; }
    }
}
