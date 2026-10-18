package com.github.ytlog.artdcevm.fixture;

/**
 * Steady-state dispatch targets for HookOverheadBenchmark. Loaded before any
 * agent attaches, so their IMTs are linked without dispatch protection.
 * Eight receivers exceed ART's polymorphic inline caches.
 */
public final class HookBenchTypes {
    public interface Shape { int area(int x); }
    public abstract static class Base {
        public int field = 3;
        public abstract int value(int x);
        public static int staticValue(int x) { return x + 1; }
    }
    public static final class E0 extends Base implements Shape {
        public int value(int x) { return x + 0; }
        public int area(int x) { return x ^ 0; }
    }
    public static final class E1 extends Base implements Shape {
        public int value(int x) { return x + 1; }
        public int area(int x) { return x ^ 1; }
    }
    public static final class E2 extends Base implements Shape {
        public int value(int x) { return x + 2; }
        public int area(int x) { return x ^ 2; }
    }
    public static final class E3 extends Base implements Shape {
        public int value(int x) { return x + 3; }
        public int area(int x) { return x ^ 3; }
    }
    public static final class E4 extends Base implements Shape {
        public int value(int x) { return x + 4; }
        public int area(int x) { return x ^ 4; }
    }
    public static final class E5 extends Base implements Shape {
        public int value(int x) { return x + 5; }
        public int area(int x) { return x ^ 5; }
    }
    public static final class E6 extends Base implements Shape {
        public int value(int x) { return x + 6; }
        public int area(int x) { return x ^ 6; }
    }
    public static final class E7 extends Base implements Shape {
        public int value(int x) { return x + 7; }
        public int area(int x) { return x ^ 7; }
    }

    // One call site per workload, so one workload never widens another's inline cache.
    public static int virtualMono(Base target, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += target.value(i);
        return sum;
    }
    public static int virtualMega(Base[] targets, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += targets[i & 7].value(i);
        return sum;
    }
    public static int interfaceMono(Shape target, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += target.area(i);
        return sum;
    }
    public static int interfaceMega(Shape[] targets, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += targets[i & 7].area(i);
        return sum;
    }
    public static int lateInterfaceMono(Shape target, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += target.area(i);
        return sum;
    }
    public static int lateInterfaceMega(Shape[] targets, int n) {
        int sum = 0;
        for (int i = 0; i < n; i++) sum += targets[i & 7].area(i);
        return sum;
    }

    private HookBenchTypes() {}
}
