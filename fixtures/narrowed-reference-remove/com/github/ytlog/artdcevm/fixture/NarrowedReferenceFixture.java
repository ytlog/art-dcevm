package com.github.ytlog.artdcevm.fixture;

public final class NarrowedReferenceFixture {
    public interface Removed { int value(); }
    public interface Child extends Removed {}
    public interface Kept { int kept(); }
    public static class Subject implements Kept {
        public Removed self;
        public Removed direct;
        public Removed indirect;
        public Kept retained;
        public int value() { return 11; }
        public int kept() { return 22; }
    }
    public static class Direct implements Removed { public int value() { return 31; } }
    public static class Inherited extends Direct {}
    public static class Indirect implements Child { public int value() { return 41; } }
    public static class Holder {
        public static int staticMarker = 9;
        public Removed removed;
        public Removed direct;
        public Removed inherited;
        public Removed indirect;
        public Kept kept;
        public Object object;
        public int version() { return 1; }
    }
    public static class HolderChild extends Holder {}
    public static class LateHolder extends Holder {}
    private NarrowedReferenceFixture() {}
}
