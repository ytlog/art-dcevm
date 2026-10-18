package com.github.ytlog.artdcevm.fixture;

/** The concrete descendants deliberately collide with Removed's IMT slot. */
public final class AbstractRemovalFixture {
    public interface Removed { int removedValue(); }
    public interface Kept { int kept31(); }
    public interface Late { int late22(); }
    public interface Spinner { void spin9(); }
    public abstract static class Task implements Spinner {
        public int runs, spins;
        public void run() { runs++; }
        public void spin9() { spins++; }
    }
    public static class ExistingTask extends Task {}
    public static class LateTask extends Task {}
    public abstract static class Subject implements Kept {
        public int marker = 5;
        public long added;
        public int removedValue() { return 11; }
        public int kept31() { return 22; }
    }
    public static class Existing extends Subject {}
    public abstract static class Bridge extends Subject {}
    public static class LateChild extends Bridge implements Late {
        public int late22() { return 33; }
    }
    public static class LateDirect extends Subject implements Late {
        public int kept31() { return 55; }
        public int late22() { return 33; }
    }
    public static Thread thread(Task task) { return new Thread((Runnable) (Object) task); }
    public static int kept(Kept kept) { return kept.kept31(); }
    public static int call(Removed removed) { return removed.removedValue(); }
    public static int failures(Subject subject, int times) {
        int failures = 0;
        for (int i = 0; i < times; ++i) {
            try { call((Removed) (Object) subject); }
            catch (IncompatibleClassChangeError expected) { failures++; }
        }
        return failures;
    }
    private AbstractRemovalFixture() {}
}
