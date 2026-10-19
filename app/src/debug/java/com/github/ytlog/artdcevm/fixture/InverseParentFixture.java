package com.github.ytlog.artdcevm.fixture;

public final class InverseParentFixture {
    public interface DefaultParent { default int valueX() { return 7; } }
    public interface AbstractParent { int valueX(); }
    public interface PrivateDefault {
        private int valueX() { return 11; }
        default int local() { return valueX(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueX(); }
    }
    public interface PrivateAbstract {
        private int valueX() { return 11; }
        default int local() { return valueX(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueX(); }
    }
    public interface StaticDefault {
        static int valueX() { return 11; }
        default int local() { return valueX(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueX(); }
    }
    public interface StaticAbstract {
        static int valueX() { return 11; }
        default int local() { return valueX(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueX(); }
    }
    public static class Peer { public int value() { return 3; } }
    public static class PrivateDefaultImpl implements PrivateDefault { public int marker = 31; }
    public static class PrivateAbstractImpl implements PrivateAbstract { public int marker = 31; }
    public static class StaticDefaultImpl implements StaticDefault { public int marker = 31; }
    public static class StaticAbstractImpl implements StaticAbstract { public int marker = 31; }
    public static class Late implements PrivateDefault {}
    public interface SharedPath extends DefaultParent {}
    public static class Shared implements PrivateDefault, SharedPath {}
    public static int local(Object receiver, int kind) {
        switch (kind) {
        case 0: return ((PrivateDefault)receiver).local();
        case 1: return ((PrivateAbstract)receiver).local();
        case 2: return ((StaticDefault)receiver).local();
        case 3: return ((StaticAbstract)receiver).local();
        default: throw new AssertionError();
        }
    }
    public static int parent(Object receiver, int kind) {
        return (kind % 2 == 0) ? ((DefaultParent)receiver).valueX() : ((AbstractParent)receiver).valueX();
    }
}
