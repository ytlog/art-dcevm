package com.github.ytlog.artdcevm.fixture;

public final class ModifierFlagsFixture {
    public interface Visible {
        default int value() { return 3; }
    }
}
