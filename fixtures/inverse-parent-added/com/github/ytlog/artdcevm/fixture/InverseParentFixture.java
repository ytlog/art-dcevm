package com.github.ytlog.artdcevm.fixture;

public final class InverseParentFixture {
    public interface DefaultParent { default int valueX() { return 9; } }
    public interface AbstractParent { int valueX(); }
    public interface PrivateDefault extends DefaultParent {
        private int valueY() { return 13; }
        default int local() { return valueY(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueY(); }
    }
    public interface PrivateAbstract extends AbstractParent {
        private int valueY() { return 13; }
        default int local() { return valueY(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueY(); }
    }
    public interface StaticDefault extends DefaultParent {
        static int valueY() { return 11; }
        default int local() { return valueY(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueY(); }
    }
    public interface StaticAbstract extends AbstractParent {
        static int valueY() { return 11; }
        default int local() { return valueY(); }
        default int await(java.util.concurrent.CountDownLatch ready, java.util.concurrent.CountDownLatch resume) throws InterruptedException { ready.countDown(); resume.await(); return valueY(); }
    }
    public static class Peer { public int value() { return 5; } }
}
