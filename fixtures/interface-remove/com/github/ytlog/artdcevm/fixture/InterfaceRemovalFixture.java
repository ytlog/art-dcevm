package com.github.ytlog.artdcevm.fixture;

public final class InterfaceRemovalFixture {
    public interface Removed {
        int removedValue();
        default int removedDefault() { return 7; }
    }
    public interface Kept { int kept31(); }
    public interface Late { int late22(); }
    public interface Spinner { void spin9(); }

    public static class Subject implements Kept {
        public int marker = 5;
        public int removedValue() { return 11; }
        public int kept31() { return 22; }
    }
    public static class Child extends Subject {}
    public static class LateChild extends Subject implements Late {
        public int late22() { return 33; }
    }
    public static class Task implements Spinner {
        public int runs, spins;
        public void run() { runs++; }
        public void spin9() { spins++; }
    }

    public static Removed held;

    public static int call(Removed removed) { return removed.removedValue(); }
    public static int viaRemoved(Subject subject) { return 0; }
    public static int callDefault(Subject subject) { return 0; }
    public static int kept(Kept kept) { return kept.kept31(); }
    public static int failures(Subject subject, int times) {
        int failed = 0;
        for (int i = 0; i < times; i++) {
            try {
                call((Removed) (Object) subject);
            } catch (IncompatibleClassChangeError expected) {
                failed++;
            }
        }
        return failed;
    }

    private InterfaceRemovalFixture() {}
}
