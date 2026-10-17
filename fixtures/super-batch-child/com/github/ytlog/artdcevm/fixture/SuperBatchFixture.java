package com.github.ytlog.artdcevm.fixture;

public final class SuperBatchFixture {
    public static class Base {
        public int baseValue = 5;
        public int base() { return baseValue + 1; }
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
    public static class Child extends Subject {
        public int childValue = 9;
        // Verifies only if the verifier sees Child under the new Subject.
        public int viaMiddle() { return Middle.accept(this) + childValue; }
    }
}
