package com.github.ytlog.artdcevm.fixture;

import java.lang.reflect.Proxy;

/** Shared, removed and simultaneously added parents of one cached Proxy class. */
public final class ProxyParentsFixture {
    public interface Shared {
        default int shared() { return 31; }
    }
    public interface Added {
        default int added() { return 41; }
        int missing();
    }
    public interface A extends Shared { int a(); }
    public interface B extends Shared { int b(); }
    public static final class Holder {
        public Shared value;
        public static Shared staticValue;
    }
    public static Object create() {
        return Proxy.newProxyInstance(A.class.getClassLoader(), new Class<?>[]{A.class, B.class},
                (proxy, method, arguments) -> {
                    switch (method.getName()) {
                        case "a": return 11;
                        case "b": return 12;
                        case "shared": return 71;
                        default: throw new AssertionError("Unexpected Proxy method");
                    }
                });
    }
    public static int shared(Shared value) { return value.shared(); }
    public static int added(Added value) { return value.added(); }
    private ProxyParentsFixture() {}
}
