package com.github.ytlog.artdcevm.fixture;

/** Declaration-only flags must preserve member identity and dispatch. */
public final class MemberDescriptorFixture {
    public interface Contract {
        Object TOKEN = new Object();
        default int value() { return 7; }
        default int sum(int[] values) { int total = 0; for (int value : values) total += value; return total; }
        default double half(double value) { return value / 2; }
        int abstractSum(int[] values);
        private int hidden() { return 5; }
        default int throughPrivate() { return hidden(); }
        static int staticValue() { return 17; }
    }
    public static class Peer {
        public Object token = new Object();
        public int value() { return 3; }
    }
    public static class Implementation implements Contract {
        public int marker = 31;
        public int abstractSum(int[] values) { int total = 0; for (int value : values) total += 2 * value; return total; }
    }
    public static class Calls {
        public static int call(Contract value) {
            return value.value() + value.sum(new int[]{2, 4}) + (int) value.half(10)
                    + value.abstractSum(new int[]{2, 4}) + value.throughPrivate() + Contract.staticValue();
        }
    }
    public static class LateCalls {
        public static int call(Contract value) {
            return value.value() + value.sum(new int[]{2, 4}) + (int) value.half(10)
                    + value.abstractSum(new int[]{2, 4}) + value.throughPrivate() + Contract.staticValue();
        }
    }
}
