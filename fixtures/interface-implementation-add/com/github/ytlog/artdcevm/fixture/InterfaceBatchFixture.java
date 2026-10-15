package com.github.ytlog.artdcevm.fixture;

public final class InterfaceBatchFixture {
    public interface Contract {
        default int value() { return 15; }
    }
    public static class Implementation implements Contract {
        public int marker = 17;
        public int added = 23;
        public int ownValue() { return marker + 42; }
        public int addedValue() { return marker + added + 51; }
    }
}
