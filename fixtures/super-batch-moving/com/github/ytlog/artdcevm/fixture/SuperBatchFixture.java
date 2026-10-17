package com.github.ytlog.artdcevm.fixture;

public final class SuperBatchFixture {
    public static class Base {
        public int baseValue = 5;
        public int base() { return baseValue; }
    }
    public static class Middle extends Base {
        public int middleValue = 11;
        public int middle() { return 40 + middleValue; }
        public static int accept(Middle middle) { return middle.middle(); }
    }
    public static class Subject extends Middle {
        public int own = 7;
    }
    public static class Step extends Subject {
    }
    public static class Child extends Step {
        public int childValue = 9;
    }
}
