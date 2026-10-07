package com.github.ytlog.artdcevm.external;

import com.github.ytlog.artdcevm.fixture.ModifierSubject;

public final class ModifierExternalOverride extends ModifierSubject {
    @Override public int compute() { return 99; }
    @Override public int packageCompute() { return 98; }
}
