package dev.artdcevm.fixture;

public interface InterfaceTag {
    int tag();
    default int defaultValue() { return 77; }
}
