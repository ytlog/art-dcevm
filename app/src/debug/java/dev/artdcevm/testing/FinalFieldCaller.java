package dev.artdcevm.testing;

import dev.artdcevm.fixture.FinalFieldSubject;

public final class FinalFieldCaller {
    private FinalFieldCaller() { }
    public static int read(FinalFieldSubject subject) { return subject.value; }
    public static void write(FinalFieldSubject subject, int value) { subject.value = value; }
    public static int readStatic() { return FinalFieldSubject.staticValue; }
    public static void writeStatic(int value) { FinalFieldSubject.staticValue = value; }
}
