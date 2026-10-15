package com.github.ytlog.artdcevm.fixture;

public final class InterfaceEvolutionFixture {
    public interface AbstractParent { int miranda(); }
    public interface MirandaRoot extends AbstractParent {
        default int stable() { return 7; }
        default int miranda() { return 99; }
    }
}
