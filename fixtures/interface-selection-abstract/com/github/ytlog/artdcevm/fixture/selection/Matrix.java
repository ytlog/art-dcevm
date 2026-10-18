package com.github.ytlog.artdcevm.fixture.selection;
public final class Matrix {
    public interface Parent { default int select() { return 17; } }
    public interface Contract {
        default int stable() { return 19; }
        int select();
    }
}
