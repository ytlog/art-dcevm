package com.github.ytlog.artdcevm.fixture;
import java.lang.reflect.Proxy;
public final class ProxyGroupFixture {
    public interface Base { int base();  }
    public interface Child extends Base { int child();  }
    public interface Side { int side();  }
    public static class Shared implements Child, Side {
        public int marker = 7;
        public int base() { return 11; }
        public int child() { return 12; }
        public int side() { return 13; }
    }
    public static Object create() {
        return Proxy.newProxyInstance(Child.class.getClassLoader(), new Class<?>[]{Child.class, Side.class},
            (proxy, method, arguments) -> {
                Class<?> type = method.getDeclaringClass();
                if (type != Base.class && type != Child.class && type != Side.class)
                    throw new AssertionError("Proxy method has an obsolete declaring interface");
                switch (method.getName()) {
                    case "base": return 21;
                    case "child": return 22;
                    case "side": return 23;
                    default: throw new AssertionError("Unexpected proxy method");
                }
            });
    }
    private ProxyGroupFixture() {}
}
