package com.github.ytlog.artdcevm.fixture;

/**
 * Child stops extending Base (fixtures/parent-remove), so Impl stops being a
 * Base. ImTable::GetImtIndex (Android 11--17) puts side21 in baseValue's IMT
 * slot: without sealing, compiled calls through Base would run it.
 */
public final class ParentRemovalFixture {
    public interface Base {
        int baseValue();
        default int baseDefault() { return 7; }
    }
    public interface Child { int childValue(); }
    public interface Side { int side21(); }

    public static class Impl implements Child, Side {
        public int baseValue() { return 11; }
        public int childValue() { return 12; }
        public int side21() { return 22; }
    }

    public static int base(Base base) { return base.baseValue(); }
    // Verified while Child extended Base: passes it on without a check.
    public static int viaChild(Child child) { return base((Base) (Object) child); }
    public static int baseDefault(Impl impl) { return 0; }
    public static int child(Child child) { return child.childValue(); }
    public static int side(Side side) { return side.side21(); }
    // Calls through Base `times` times; `base` turns hot and is compiled.
    public static int failures(Child child, int times) {
        int failed = 0;
        for (int i = 0; i < times; i++) {
            try {
                viaChild(child);
            } catch (IncompatibleClassChangeError expected) {
                failed++;
            }
        }
        return failed;
    }

    private ParentRemovalFixture() {}
}
