package dev.artdcevm.gc;

public interface AotInterfaceTag {
    int tag();
    default int defaultValue() { return 77; }
}
