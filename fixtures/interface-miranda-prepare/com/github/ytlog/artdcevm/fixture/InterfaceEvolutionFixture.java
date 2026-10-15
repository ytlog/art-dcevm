package com.github.ytlog.artdcevm.fixture;

public final class InterfaceEvolutionFixture {
    public interface AbstractParent { int miranda(); }
    public interface MirandaRoot extends AbstractParent {
        default int stable() { return 7; }

    }
    public static abstract class MirandaBase implements MirandaRoot {}
    public static class MirandaImpl extends MirandaBase {
        public int miranda() { return 9; }
    }
}
