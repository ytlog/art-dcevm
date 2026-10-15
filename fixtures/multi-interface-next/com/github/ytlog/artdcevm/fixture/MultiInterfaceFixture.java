package com.github.ytlog.artdcevm.fixture;

public final class MultiInterfaceFixture {
    public interface Left {
        default int leftValue() { return 13; }
    }
    public interface Right {
        default int rightValue() { return 23; }
    }
    public interface Parent {
        default int parentValue() { return 33; }
    }
    public interface Child extends Parent {
        default int childValue() { return 43; }
    }
}
