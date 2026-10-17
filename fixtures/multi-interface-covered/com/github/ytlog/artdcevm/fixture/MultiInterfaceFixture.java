package com.github.ytlog.artdcevm.fixture;

public final class MultiInterfaceFixture {
    public interface CoveredLeft {
        default int coveredLeft() { return 52; }
    }
    public interface CoveredRight {
        default int coveredRight() { return 62; }
    }
}
