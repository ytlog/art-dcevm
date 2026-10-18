package com.github.ytlog.artdcevm.fixture;

/** Reference fields whose declared types change keep their compatible values. */
public final class RetypeFixture {
    private RetypeFixture() {}

    public static final class Task implements Runnable {
        @Override public void run() {}
    }

    // Runnable through a superinterface only, and through a superclass.
    public interface SubTask extends Runnable {}
    public static final class Deep implements SubTask {
        @Override public void run() {}
    }
    public static class TaskBase implements Runnable {
        @Override public void run() {}
    }
    public static final class InheritedTask extends TaskBase {}

    public static class Subject {
        public CharSequence text;
        public Number number;
        public Number mismatch;
        public Runnable task;
        public Runnable deep;
        public Runnable inherited;
        public int keep = 41;
        public static CharSequence label;
        public static Runnable staticMismatch;
    }

    public static class Child extends Subject {
        public int own = 17;
    }
}
