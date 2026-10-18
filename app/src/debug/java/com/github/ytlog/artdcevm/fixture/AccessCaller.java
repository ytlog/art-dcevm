package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** Old virtual, super and interface call sites, outside the target's nest. */
public final class AccessCaller extends AccessCallerBase {
    public static class Child extends AccessSubject {
        public int throughSuper() { return super.target(); }
    }
    public static class Override extends AccessSubject {
        @java.lang.Override public int target() { return 47; }
    }
    public static class InterfaceOverride implements InterfaceInvokeKindFixture.Contract {
        @java.lang.Override public int value() { return 49; }
    }
    public static int callTarget(AccessSubject subject) { return subject.target(); }
    public static int inherited(Child child) { return child.target(); }
    public static int overridden(Override child) { return child.target(); }
    public static int range(AccessSubject subject) { return subject.range(1L, 2L, 3L); }
    public static int interfaceCall(InterfaceInvokeKindFixture.Contract value) { return value.value(); }
    public static int interfaceVirtual(InterfaceInvokeKindFixture.Impl value) { return value.value(); }
    public static int interfaceOverride(InterfaceOverride value) { return value.value(); }
    public static int await(AccessSubject value, CountDownLatch ready, CountDownLatch resume)
            throws InterruptedException {
        ready.countDown(); resume.await(); return value.target();
    }
    public static int hotFailures(AccessSubject value, int count) {
        int failures = 0;
        for (int i = 0; i < count; i++) {
            try { callTarget(value); } catch (IllegalAccessError expected) { failures++; }
        }
        return failures;
    }
}
