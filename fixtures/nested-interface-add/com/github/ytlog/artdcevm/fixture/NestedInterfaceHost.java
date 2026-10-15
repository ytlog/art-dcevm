package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract {
        default int value() { return 42; }
        default int added() { return 43; }
    }
}
