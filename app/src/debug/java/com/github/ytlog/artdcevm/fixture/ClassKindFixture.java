package com.github.ytlog.artdcevm.fixture;

public final class ClassKindFixture {
    public static class ClassTarget { public int marker=19; public Object reference="kept"; public int value(){return 7;} public static int stable=23; }
    public interface InterfaceTarget { default int value(){return 11;} }
    public @interface AnnotationTarget { int value() default 13; }
    public interface LiveTarget { default int value(){return 11;} }
}
