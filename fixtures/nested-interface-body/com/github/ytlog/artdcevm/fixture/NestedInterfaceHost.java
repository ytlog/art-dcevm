package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract {
        default int value() { return 42; }
    }
}
