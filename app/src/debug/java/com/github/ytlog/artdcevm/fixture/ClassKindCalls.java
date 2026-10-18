package com.github.ytlog.artdcevm.fixture;

public final class ClassKindCalls {
    public static int classCall(Object value) { return ((ClassKindFixture.ClassTarget) value).value(); }
    public static int interfaceCall(Object value) { return ((ClassKindFixture.InterfaceTarget) value).value(); }
    public static int annotationCall(Object value) { return ((ClassKindFixture.AnnotationTarget) value).value(); }
    public static int liveCall(Object value) { return ((ClassKindFixture.LiveTarget) value).value(); }
    public static boolean classIs(Object value) { return value instanceof ClassKindFixture.ClassTarget; }
    public static Object classCast(Object value) { return (ClassKindFixture.ClassTarget) value; }
    public static Object interfaceCast(Object value) { return (ClassKindFixture.InterfaceTarget) value; }
    public static Object annotationCast(Object value) { return (ClassKindFixture.AnnotationTarget) value; }
    public static Object construct() { return new ClassKindFixture.ClassTarget(); }
    public static class LiveImpl implements ClassKindFixture.LiveTarget {
        public int marker=43;
        public int value() { return 31; }
        public Class<? extends java.lang.annotation.Annotation> annotationType() { return null; }
    }
}
