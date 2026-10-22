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
        public int number() { observe(7, false); return 7; }
        public boolean truth() { return false; }
        public byte tiny() { return 2; }
        public char character() { return 'A'; }
        public short small() { return 12; }
        public long wide(long value) { return value + 7; }
        public float fraction() { return 1.25f; }
        public double real(double value) { return value * 2 + 7; }
        public String text(String value) { return value + "-old7"; }
        public Object identity(Object value) { return null; }
        public void effect(int[] box) { box[0] += 1; }
        public int fail() { throw new IllegalStateException("old"); }
        public synchronized int locked() { return Thread.holdsLock(this) ? 7 : -1; }
        public int recurse(int depth) { return number() + (depth == 0 ? 0 : recurse(depth - 1)); }
        public int range(long a, long b, long c) { return (int)(a + b + c) + 7; }
        public int reenter(Callable<Object> action) throws Exception { return (Integer)action.call() + 1; }
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
                default: throw new AssertionError("mode");
            }
        }
    }
    public static class Child extends Subject { public int number() { return 47; } }
    public interface Contract {
        default int number() { observe(11, false); return 11; }
        default boolean truth() { return false; }
        default byte tiny() { return 2; }
        default char character() { return 'A'; }
        default short small() { return 12; }
        default long wide(long value) { return value + 11; }
        default float fraction() { return 1.25f; }
        default double real(double value) { return value * 2 + 11; }
        default String text(String value) { return value + "-old11"; }
        default Object identity(Object value) { return null; }
        default void effect(int[] box) { box[0] += 1; }
        default int fail() { throw new IllegalStateException("old"); }
        default int locked() { synchronized (this) { return Thread.holdsLock(this) ? 11 : -1; } }
        default int recurse(int depth) { return number() + (depth == 0 ? 0 : recurse(depth - 1)); }
        default int range(long a, long b, long c) { return (int)(a + b + c) + 11; }
        default int reenter(Callable<Object> action) throws Exception { return (Integer)action.call() + 1; }
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
                default: throw new AssertionError("mode");
            }
        }
    }
    public static class Impl implements Contract {}
    public static class Override implements Contract { public int number() { return 49; } }
    public static class Peer {
        public int number() { return 7; }
        public int await(Gate gate) throws Exception { int saved = 3; gate.entered.countDown(); gate.resume.await(); return number() * 10 + saved; }
    }
}
