package dev.artdcevm.external;

import dev.artdcevm.fixture.ModifierSubject;

public final class ModifierExternalOverride extends ModifierSubject {
    @Override public int compute() { return 99; }
    @Override public int packageCompute() { return 98; }
}
