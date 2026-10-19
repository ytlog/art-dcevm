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

}
