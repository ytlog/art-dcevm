package com.github.ytlog.artdcevm.fixture;

public final class InterfaceKindFixture {
    public interface Contract {
        int required();
        default int optional() { return 11; }
    }
    public abstract static class Partial implements Contract {
        public int marker = 3;
    }
    public static class Concrete extends Partial {
        public int required() { return 41; }
    }
}
