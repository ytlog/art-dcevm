package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOverlapFixture {
    public interface Contract {
        default int value() { return 31; }
    }
    public static class Base {
    }
    public static class Parent {
        public int parentValue() { return 21; }
    }
    public static class Implementation extends Parent implements Contract {
        public int marker = 17;
    }
    // Loaded late: neither Contract nor Parent then covers every instance.
    public static class OtherImplementation implements Contract {
    }
}
