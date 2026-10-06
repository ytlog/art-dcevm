package dev.artdcevm.testing;

import dev.artdcevm.fixture.FinalFieldSubject;
import java.lang.invoke.MethodHandle;

public final class FinalFieldHandleCaller {
    private static MethodHandle instanceSetter;
    private static MethodHandle staticSetter;

    private FinalFieldHandleCaller() { }

    public static void initialize(MethodHandle instance, MethodHandle staticField) {
        instanceSetter = instance;
        staticSetter = staticField;
    }

    public static void writeInstance(FinalFieldSubject subject, int value) throws Throwable {
        instanceSetter.invokeExact(subject, value);
    }

    public static void writeStatic(int value) throws Throwable {
        staticSetter.invokeExact(value);
    }
}
