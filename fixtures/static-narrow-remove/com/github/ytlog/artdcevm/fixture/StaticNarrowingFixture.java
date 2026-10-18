package com.github.ytlog.artdcevm.fixture;
public final class StaticNarrowingFixture {
    public interface Base { int value(); }
    public interface Child  { int child(); }
    public interface Side { int side(); default int added() { return 33; } }
    public static class Shared implements Child, Side {
        public int marker = 7;
        public static Object[] array;
        public static Object objectArray;
        public static Cloneable cloneableArray;
        public static java.io.Serializable serializableArray;
        public static Base[] covariant;
        public static Base[][] matrix;
        public static Base unsubmitted;
        public static Base[] retypedArray;
        public static Base self;
        public static Base compatible;
        public static Base retyped;
        public int value() { return 11; }
        public int child() { return 12; }
        public int side() { return 13; }
    }
    public static class Compatible implements Base { public int value() { return 22; } }
    public static class CompatibleChild extends Compatible {}
    public static class Unsubmitted implements Base { public int value() { return 31; } }
    public static class Holder {
        public static Object[] array;
        public static Object objectArray;
        public static Cloneable cloneableArray;
        public static java.io.Serializable serializableArray;
        public static Base[] covariant;
        public static Base[][] matrix;
        public static Base unsubmitted;
        public static Base[] retypedArray;
        public static Base self;
        public static Base compatible;
        public int added;
    }
    private StaticNarrowingFixture() {}
}
