package dev.artdcevm.external;

import dev.artdcevm.fixture.ModifierSubject;

public final class ModifierExternalCaller {
    public static int field(ModifierSubject subject) { return subject.value; }
    public static int method(ModifierSubject subject) { return subject.compute(); }
    public static int packageField(ModifierSubject subject) { return subject.packageValue; }
    public static int packageMethod(ModifierSubject subject) { return subject.packageCompute(); }
}
