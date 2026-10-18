package com.github.ytlog.artdcevm.fixture;

/**
 * Subject and Task stop implementing an interface (fixtures/interface-remove).
 * Names are chosen so that ImTable::GetImtIndex (Android 11--17) puts kept31
 * and late22 in removedValue's IMT slot and spin9 in Runnable.run's: without
 * sealing, compiled calls through the removed interface would run them.
 */
public final class InterfaceRemovalFixture {
    public interface Removed {
        int removedValue();
        default int removedDefault() { return 7; }
    }
    public interface Kept { int kept31(); }
    public interface Late { int late22(); }
    public interface Spinner { void spin9(); }

    public static class Subject implements Removed, Kept {
        public int marker = 5;
        // The subject itself, typed with an interface it may stop implementing.
        public static Removed self;
        public static Kept keptSelf;
        public int removedValue() { return 11; }
        public int kept31() { return 22; }
    }
    public static class Child extends Subject {}
    // Loaded only after the removal; its IMT starts from Subject's.
    public static class LateChild extends Subject implements Late {
        public int late22() { return 33; }
    }
    public static class Task implements Runnable, Spinner {
        public int runs, spins;
        public void run() { runs++; }
        public void spin9() { spins++; }
    }

    public static Removed held;

    public static int call(Removed removed) { return removed.removedValue(); }
    // Verified while Subject implemented Removed: passes it on without a check.
    public static int viaRemoved(Subject subject) { return call(subject); }
    public static int callDefault(Subject subject) { return subject.removedDefault(); }
    public static int kept(Kept kept) { return kept.kept31(); }
    // Calls through Removed `times` times; `call` turns hot and is compiled.
    public static int failures(Subject subject, int times) {
        int failed = 0;
        for (int i = 0; i < times; i++) {
            try {
                call(subject);
            } catch (IncompatibleClassChangeError expected) {
                failed++;
            }
        }
        return failed;
    }

    private InterfaceRemovalFixture() {}
}
