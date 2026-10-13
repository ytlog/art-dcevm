package com.github.ytlog.artdcevm.fixture;

/** Bytecode field access compiled against the original FieldSubject. */
public final class FieldCaller {
    private FieldCaller() {}

    public static int readI(FieldSubject subject) { return subject.i; }
    public static void writeJ(FieldSubject subject, long value) { subject.j = value; }
    public static Object readO(FieldSubject subject) { return subject.o; }
    public static int readSi() { return FieldSubject.si; }
    public static void writeSo(Object value) { FieldSubject.so = value; }
    public static int readKeep(FieldSubject subject) { return subject.keep; }
}
