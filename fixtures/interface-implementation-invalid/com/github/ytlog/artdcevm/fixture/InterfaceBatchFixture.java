package com.github.ytlog.artdcevm.fixture;

public final class InterfaceBatchFixture {
    public interface Contract {
        default int value() { return 18; }
    }
    public static class Implementation implements Contract {
        public int marker = 17;
        public static int ownValue() { return 99; }
    }
}
