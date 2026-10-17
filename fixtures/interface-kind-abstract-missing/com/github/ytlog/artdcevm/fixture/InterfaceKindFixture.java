package com.github.ytlog.artdcevm.fixture;

public final class InterfaceKindFixture {
    public interface Contract {
        default int required() { return 12; }
        int optional();
    }
}
