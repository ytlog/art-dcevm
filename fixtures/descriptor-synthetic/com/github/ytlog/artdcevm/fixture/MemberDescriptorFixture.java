package com.github.ytlog.artdcevm.fixture;

/** Declaration-only flags must preserve member identity and dispatch. */
public final class MemberDescriptorFixture {
    public interface Contract {
        Object TOKEN = new Object();
        default int value() { return 11; }
        default int sum(int[] values) { int total = 1; for (int value : values) total += value; return total; }
        default double half(double value) { return value / 4; }
        int abstractSum(int[] values);
        private int hidden() { return 9; }
        default int throughPrivate() { return hidden(); }
        static int staticValue() { return 23; }
    }
    public static class Peer {
        public Object token = new Object();
        public int value() { return 13; }
    }

}
