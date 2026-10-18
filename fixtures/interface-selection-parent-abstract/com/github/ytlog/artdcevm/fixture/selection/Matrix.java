package com.github.ytlog.artdcevm.fixture.selection;
public final class Matrix {
    public interface Parent { int select(); }
    public interface Contract extends Parent { default int stable() { return 19; } }
}
