package com.github.ytlog.artdcevm.fixture.selection;
public final class Matrix {
    public interface Parent { default int select() { return 17; } }
    public interface Contract extends Parent {
        default int stable() { return 19; }

    }
}
