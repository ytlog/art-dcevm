package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;

/** Real old frames retain locals across commits; every result is used afterwards. */
public final class PrivateContinuationFixture {
    private PrivateContinuationFixture() {}
    public static final int MODES = 20;
    public static volatile Gate hot;
    public static final class Gate {
        public final CountDownLatch entered;
        public final CountDownLatch resume = new CountDownLatch(1);
        public volatile boolean done;
        public volatile int observed, spins, finallyHits;
        public volatile boolean privateObserved;
        public Object receiver;
        public Callable<Object> action;
        public Gate(int workers) { entered = new CountDownLatch(workers); }
    }
    public static void observe(int value, boolean privateBody) {
        Gate gate = hot;
        if (gate != null) { gate.observed = value; if (privateBody) gate.privateObserved = true; }
    }
    public static class Subject {
        public int field = 5;
        private int number() { observe(13, true); return 13; }
        private boolean truth() { return true; }
        private byte tiny() { return -12; }
        private char character() { return '\u754c'; }
        private short small() { return -2345; }
        private long wide(long value) { return value + 13; }
        private float fraction() { return 2.5f; }
        private double real(double value) { return value * 2 + 13; }
        private String text(String value) { return value + "-new13"; }
        private Object identity(Object value) { return value; }
        private void effect(int[] box) { box[0] += 13; }
        private int fail() { throw new IllegalArgumentException("new"); }
        private synchronized int locked() { return Thread.holdsLock(this) ? 13 : -1; }
        private int recurse(int depth) { return number() + (depth == 0 ? 0 : recurse(depth - 1)); }
        private int range(long a, long b, long c) { return (int)(a + b + c) + 13; }
        private int reenter(Callable<Object> action) throws Exception { return (Integer)action.call() + 5; }
        public int fresh() { return number() * 3 + 1; }
        public int oldRecursion(Gate gate, int depth) throws Exception {
            int saved = depth;
            if (depth == 0) { gate.entered.countDown(); gate.resume.await(); }
            else saved += oldRecursion(gate, depth - 1);
            return number() + saved;
        }
        public Object await(Gate gate, int mode) throws Exception {
            int saved = 41;
            Object keep = new int[]{9};
            gate.entered.countDown();
            if (mode == 20) { while (!gate.done) { number(); gate.spins++; } return number() * 3 + saved; }
            gate.resume.await();
            switch (mode) {
                case 0: return number() * 3 + 1;
                case 1: return truth() ? "yes" : "no";
                case 2: return tiny() + 100;
                case 3: return character() + 1;
                case 4: return small() + 2;
                case 5: return wide(0x123456780000L) + 1L;
                case 6: return fraction() * 4f + 1f;
                case 7: return real(1.25) * 100d + 1d;
                case 8: return text("v") + "!";
                case 9: { Object result = identity(keep); System.gc(); System.gc(); return result == keep && ((int[])result)[0] == 9 ? "identity-kept" : "identity-lost"; }
                case 10: { int[] box = {3}; effect(box); return box[0] * 10 + 1; }
                case 11: try { fail(); return "missing"; } catch (RuntimeException error) { return error.getClass().getName() + ":" + saved; } finally { gate.finallyHits++; }
                case 12: try { return fail(); } finally { gate.finallyHits++; }
                case 13: synchronized (this) { int result = number(); return Thread.holdsLock(this) ? result + 1000 : -1; }
                case 14: return locked() + 100;
                case 15: return recurse(3) + 1;
                case 16: { int a = number(); System.gc(); return a * 1000 + number(); }
                case 17: return range(1L, 2L, 3L) * 10 + 1;
                case 18: { Subject receiver = (Subject)gate.receiver; return receiver.number(); }
                case 19: return reenter(gate.action) * 3 + 1;
                case 21: {
                    int before = number();
                    try { return before * 10000 + reenter(gate.action) * 100 + number() + saved; }
                    catch (IllegalStateException error) { return error.getMessage() + ":" + (before * 10000 + saved * 100 + number()); }
                    finally { gate.finallyHits++; }
                }
                default: throw new AssertionError("mode");
            }
        }
    }
    public static class Child extends Subject { public int number() { return 47; } }
    public interface Contract {
        private int number() { observe(31, true); return 31; }
        private boolean truth() { return true; }
        private byte tiny() { return -12; }
        private char character() { return '\u754c'; }
        private short small() { return -2345; }
        private long wide(long value) { return value + 31; }
        private float fraction() { return 2.5f; }
        private double real(double value) { return value * 2 + 31; }
        private String text(String value) { return value + "-new31"; }
        private Object identity(Object value) { return value; }
        private void effect(int[] box) { box[0] += 31; }
        private int fail() { throw new IllegalArgumentException("new"); }
        private int locked() { synchronized (this) { return Thread.holdsLock(this) ? 31 : -1; } }
        private int recurse(int depth) { return number() + (depth == 0 ? 0 : recurse(depth - 1)); }
        private int range(long a, long b, long c) { return (int)(a + b + c) + 31; }
        private int reenter(Callable<Object> action) throws Exception { return (Integer)action.call() + 5; }
        default int fresh() { return number() * 3 + 1; }
        default int oldRecursion(Gate gate, int depth) throws Exception {
            int saved = depth;
            if (depth == 0) { gate.entered.countDown(); gate.resume.await(); }
            else saved += oldRecursion(gate, depth - 1);
            return number() + saved;
        }
        default Object await(Gate gate, int mode) throws Exception {
            int saved = 41;
            Object keep = new int[]{9};
            gate.entered.countDown();
            if (mode == 20) { while (!gate.done) { number(); gate.spins++; } return number() * 3 + saved; }
            gate.resume.await();
            switch (mode) {
                case 0: return number() * 3 + 1;
                case 1: return truth() ? "yes" : "no";
                case 2: return tiny() + 100;
                case 3: return character() + 1;
                case 4: return small() + 2;
                case 5: return wide(0x123456780000L) + 1L;
                case 6: return fraction() * 4f + 1f;
                case 7: return real(1.25) * 100d + 1d;
                case 8: return text("v") + "!";
                case 9: { Object result = identity(keep); System.gc(); System.gc(); return result == keep && ((int[])result)[0] == 9 ? "identity-kept" : "identity-lost"; }
                case 10: { int[] box = {3}; effect(box); return box[0] * 10 + 1; }
                case 11: try { fail(); return "missing"; } catch (RuntimeException error) { return error.getClass().getName() + ":" + saved; } finally { gate.finallyHits++; }
                case 12: try { return fail(); } finally { gate.finallyHits++; }
                case 13: synchronized (this) { int result = number(); return Thread.holdsLock(this) ? result + 1000 : -1; }
                case 14: return locked() + 100;
                case 15: return recurse(3) + 1;
                case 16: { int a = number(); System.gc(); return a * 1000 + number(); }
                case 17: return range(1L, 2L, 3L) * 10 + 1;
                case 18: { Contract receiver = (Contract)gate.receiver; return receiver.number(); }
                case 19: return reenter(gate.action) * 3 + 1;
                case 21: {
                    int before = number();
                    try { return before * 10000 + reenter(gate.action) * 100 + number() + saved; }
                    catch (IllegalStateException error) { return error.getMessage() + ":" + (before * 10000 + saved * 100 + number()); }
                    finally { gate.finallyHits++; }
                }
                default: throw new AssertionError("mode");
            }
        }
    }
    public static class Impl implements Contract {}
    public static class Override implements Contract { public int number() { return 49; } }
    public static class Peer {
        private int number() { return 17; }
        public int await(Gate gate) throws Exception { int saved = 3; gate.entered.countDown(); gate.resume.await(); return number() * 10 + saved; }
    }
}
