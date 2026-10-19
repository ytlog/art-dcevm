package com.github.ytlog.artdcevm.fixture;

/** Ordinary targets with and without an implementation of an inherited default. */
public final class ReceiverLossFixture {
    public interface Contract {
        default int value() { return 7; }
        default int bodyError() { throw new IllegalStateException("target body"); }
    }
    public static class DefaultOnly implements Contract {
        public int kept = 19;
        public Object self = this;
    }
    public static class Own implements Contract {
        public int kept = 23;
        public Object self = this;
        public int value() { return 11; }
    }
    public static class Peer { public int value() { return 3; } }
}
