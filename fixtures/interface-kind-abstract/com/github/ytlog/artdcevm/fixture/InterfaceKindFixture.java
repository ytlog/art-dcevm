package com.github.ytlog.artdcevm.fixture;

public final class InterfaceKindFixture {
    public interface Contract {
        default int required() { return 12; }
        int optional();
    }
    public abstract static class Partial implements Contract {
        public int marker = 3;
    }
    public static class Concrete extends Partial {
        public int optional() { return 51; }
    }
}
