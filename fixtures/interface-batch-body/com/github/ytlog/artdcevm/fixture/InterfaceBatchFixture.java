package com.github.ytlog.artdcevm.fixture;

public final class InterfaceBatchFixture {
    public interface Contract {
        default int value() { return 12; }
    }
    public static class Unrelated {
        public int marker = 19;
        public int value() { return 22; }
    }
}
