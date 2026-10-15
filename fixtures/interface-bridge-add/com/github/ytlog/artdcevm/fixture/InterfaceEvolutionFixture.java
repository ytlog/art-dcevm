package com.github.ytlog.artdcevm.fixture;

public final class InterfaceEvolutionFixture {
    public interface BridgeParent<T> { T result(); }
    public interface BridgeContract extends BridgeParent<Integer> {
        default Integer result() { return 90; }
        default int added() { return 91; }
    }
    public static class BridgeImpl implements BridgeContract {
        public Integer result() { return BridgeContract.super.result(); }
        public int added() { return BridgeContract.super.added(); }
        public static int recipePadding() { return 0; }
    }
}
