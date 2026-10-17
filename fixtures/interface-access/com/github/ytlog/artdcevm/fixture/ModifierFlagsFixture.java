package com.github.ytlog.artdcevm.fixture;

public final class ModifierFlagsFixture {
    interface Visible {
        default int value() { return 2; }
    }
}
