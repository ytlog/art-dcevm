package dev.artdcevm.fixture;
public final class FieldCaller {
    public static long hot(FieldSubject x) { return x.j; }
    public static long cold(FieldSubject x) { return x.j; }
    public static void set(FieldSubject x) { x.j = 123; }
    public static int staticGet() { return FieldSubject.si; }
    public static void staticSet() { FieldSubject.si = 123; }
    public static long active(FieldSubject x) {
        long before = x.j;
        DeletionControl.awaitRelease();
        return before + x.j;
    }
}
