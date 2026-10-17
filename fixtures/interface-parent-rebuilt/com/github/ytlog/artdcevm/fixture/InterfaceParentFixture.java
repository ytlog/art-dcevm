package com.github.ytlog.artdcevm.fixture;

public final class InterfaceParentFixture {
    public interface Parent {
        int parentRequired();
        default int parentValue() { return 22; }
    }
    public interface Extra {
        default int extraValue() { return 32; }
    }
    public interface Contract extends Parent, Extra {
        default int value() { return 13; }
    }
}
