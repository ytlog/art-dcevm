package com.github.ytlog.artdcevm.fixture;

/** Bytecode calls into NativeSubject; call only after its natives were replaced. */
public final class NativeCaller {
    private NativeCaller() {}
    public static int nativeValue(NativeSubject subject) { return subject.nativeValue(); }
    public static int nativeToJava(NativeSubject subject) { return subject.nativeToJava(); }
}
