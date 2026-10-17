package com.github.ytlog.artdcevm.fixture;

public final class NestedInterfaceHost {
    public interface Contract {
        java.util.List<String> NAMES = new java.util.ArrayList<>();
        default int value() { return 47; }
    }
}
