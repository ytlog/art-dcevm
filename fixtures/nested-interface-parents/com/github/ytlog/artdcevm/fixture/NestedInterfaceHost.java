package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract extends Runnable {
        default int value() { return 44; }
    }
}
