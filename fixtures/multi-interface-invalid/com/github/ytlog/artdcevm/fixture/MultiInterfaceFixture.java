package com.github.ytlog.artdcevm.fixture;

public final class MultiInterfaceFixture {
    public interface Left {
        default int leftValue() { return 12; }
    }
    public interface Right {
        default int rightValue() { return 22; }
        default int added() { return 99; }
    }
    public interface Parent {
        default int parentValue() { return 32; }
    }
    public interface Child extends Parent {
        default int childValue() { return 42; }
    }
}
