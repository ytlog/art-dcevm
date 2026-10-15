package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract {
        int added = 44; default int value() { return 44; }
    }
}
