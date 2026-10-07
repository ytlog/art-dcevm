package com.github.ytlog.artdcevm.fixture;

public interface InterfaceTag {
    int tag();
    default int defaultValue() { return 77; }
}
