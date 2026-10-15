package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract {
        default int value() { return 41; }
    }

    public static final class Implementation implements Contract {
        public int marker = 17;
    }
}
