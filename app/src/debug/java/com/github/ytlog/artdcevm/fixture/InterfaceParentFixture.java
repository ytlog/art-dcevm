package com.github.ytlog.artdcevm.fixture;

public final class InterfaceParentFixture {
    public interface Parent {
        int parentRequired();
        default int parentValue() { return 21; }
    }
    public interface Contract {
        default int value() { return 11; }
    }
    public static class Implementation implements Contract {
        public int marker = 5;
    }
    public static class Complete implements Contract {
        public int parentRequired() { return 31; }
    }
    public static class Plain {
        public int parentRequired() { return 51; }
    }
}
