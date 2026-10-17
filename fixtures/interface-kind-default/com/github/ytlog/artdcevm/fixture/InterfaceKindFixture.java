package com.github.ytlog.artdcevm.fixture;

public final class InterfaceKindFixture {
    public interface Contract {
        default int required() { return 12; }
        default int optional() { return 11; }
    }
    public static class Concrete extends Partial {
    }
    public abstract static class Partial implements Contract {
        public int marker = 3;
    }
}
