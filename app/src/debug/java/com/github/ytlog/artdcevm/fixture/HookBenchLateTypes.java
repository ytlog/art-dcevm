package com.github.ytlog.artdcevm.fixture;

/**
 * Receivers HookOverheadBenchmark links only after selecting its state, by
 * name: after an interface removal ART links them with sealed IMTs.
 */
public final class HookBenchLateTypes {
    public static final class L0 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 0; }
        public int area(int x) { return x ^ 8; }
    }
    public static final class L1 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 1; }
        public int area(int x) { return x ^ 9; }
    }
    public static final class L2 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 2; }
        public int area(int x) { return x ^ 10; }
    }
    public static final class L3 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 3; }
        public int area(int x) { return x ^ 11; }
    }
    public static final class L4 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 4; }
        public int area(int x) { return x ^ 12; }
    }
    public static final class L5 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 5; }
        public int area(int x) { return x ^ 13; }
    }
    public static final class L6 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 6; }
        public int area(int x) { return x ^ 14; }
    }
    public static final class L7 extends HookBenchTypes.Base implements HookBenchTypes.Shape {
        public int value(int x) { return x - 7; }
        public int area(int x) { return x ^ 15; }
    }

    private HookBenchLateTypes() {}
}
