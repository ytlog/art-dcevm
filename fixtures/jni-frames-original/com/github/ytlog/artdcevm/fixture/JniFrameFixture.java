package com.github.ytlog.artdcevm.fixture;

public final class JniFrameFixture {
    public interface Contract { default int value() { JniFrameGate.pause(0); return JniFrameGate.checked(this, 7); } }
    public static class DefaultOnly implements Contract {
        public int kept = 19;
        public Object self = this;
    }
    public static class Own implements Contract {
        public int kept = 23;
        public Object self = this;
        public int value() { JniFrameGate.pause(0); return JniFrameGate.checked(this, 11); }
    }
    public static class Peer { public int value() { return 3; } }
}
