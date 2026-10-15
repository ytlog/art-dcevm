package com.github.ytlog.artdcevm.fixture;

public final class InterfaceEvolutionFixture {
    public interface Parent { default int inherited() { return 20; } }
    public interface Root extends Parent {
        default int value() { return 1; }
        default int added() { return 60; }
        default int inherited() { return 50; }
    }
}
