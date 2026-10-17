package com.github.ytlog.artdcevm.fixture;

public final class InterfaceParentFixture {
    public interface Parent {
        int parentRequired();
        default int parentValue() { return 21; }
    }
    public interface Contract extends Parent {
        default int value() { return 12; }
    }
    public static class Implementation implements Contract {
        public int marker = 5;
        public int parentRequired() { return 41; }
    }
}
