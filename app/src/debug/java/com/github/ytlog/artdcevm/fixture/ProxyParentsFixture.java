package com.github.ytlog.artdcevm.fixture;

import java.lang.reflect.Proxy;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

/** Shared, removed and simultaneously added parents of one cached Proxy class. */
public final class ProxyParentsFixture {
    public interface Shared {
        default int shared() { return 31; }
    }
    public interface Added {
        default int added() { return 41; }
        int missing();
    }
    public interface Root { default int root() { return 45; } }
    public interface A extends Shared { int a(); }
    public interface B extends Shared { int b(); }
    public static final class Holder {
        public Shared value;
        public Added added;
        public Root root;
        public Object object;
        public static Shared staticValue;
    }
    public static final class RecordingHandler implements InvocationHandler {
        public Method last;
        public int calls;
        @Override public Object invoke(Object proxy, Method method, Object[] arguments) {
            last = method;
            calls++;
            switch (method.getName()) {
                case "a": return 11;
                case "b": return 12;
                case "shared": return 71;
                default: throw new AssertionError("Unexpected Proxy method: " + method.getName());
            }
        }
    }
    public static Object create() {
        return create(new RecordingHandler());
    }
    public static Object create(InvocationHandler handler) {
        return Proxy.newProxyInstance(A.class.getClassLoader(), new Class<?>[]{A.class, B.class}, handler);
    }
    public static Object createA(InvocationHandler handler) {
        return Proxy.newProxyInstance(A.class.getClassLoader(), new Class<?>[]{A.class}, handler);
    }
    public static int shared(Shared value) { return value.shared(); }
    public static int added(Added value) { return value.added(); }
    public static int sharedFailures(Shared value, int times) {
        int failures = 0;
        for (int i = 0; i < times; i++) {
            try {
                if (value.shared() != 71) throw new AssertionError("Unexpected shared dispatch");
            } catch (IncompatibleClassChangeError error) {
                if (error.getClass() != IncompatibleClassChangeError.class) throw error;
                failures++;
            }
        }
        return failures;
    }
    public static int addedSum(Added value, int times) {
        int sum = 0;
        for (int i = 0; i < times; i++) sum += value.added();
        return sum;
    }
    public static int rootSum(Root value, int times) {
        int sum = 0;
        for (int i = 0; i < times; i++) sum += value.root();
        return sum;
    }
    private ProxyParentsFixture() {}
}
