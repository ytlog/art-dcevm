package com.github.ytlog.artdcevm.fixture;

/** Reference fields whose declared types change keep their compatible values. */
public final class RetypeFixture {
    private RetypeFixture() {}

    public static final class Task implements Runnable {
        @Override public void run() {}
    }

    public static class Subject {
        public String text = "kept";
        public Object number = Integer.valueOf(5);
        public Object mismatch = "text";
        public Object task = new Task();
        public int keep = 41;
        public static String label = "static";
        public static Object staticMismatch = new Object();
    }

    public static class Child extends Subject {
        public int own = 17;
    }
}
