package com.github.ytlog.artdcevm.fixture;

public final class InterfaceParentFixture {
    public interface Contract {
        default int value() { return 11; }
    }
}
