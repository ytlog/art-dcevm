package com.github.ytlog.artdcevm.fixture;

public final class ConstructorHistoryFixture {
    public abstract static class Cold { public Cold(int value) {} }
    public abstract static class Warm { public Warm(int value) {} }
    public abstract static class Arity { public Arity(int value) {} }
    public abstract static class Type { public Type(int value) {} }
}
