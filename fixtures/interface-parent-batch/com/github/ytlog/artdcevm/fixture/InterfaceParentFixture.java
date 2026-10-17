package com.github.ytlog.artdcevm.fixture;

public final class InterfaceParentFixture {
    public interface Parent {
        int parentRequired();
        default int parentValue() { return 22; }
    }
    public static class Plain implements Parent {
        public int parentRequired() { return 51; }
    }
}
