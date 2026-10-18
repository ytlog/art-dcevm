package com.github.ytlog.artdcevm.fixture;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Proxy;

/** A java.lang.reflect.Proxy implementation of a redefined interface (fixtures/proxy-api-*). */
public final class ProxyFixture {
    public interface Api {
        int get();
        int added();
        default int twice() { return get() * 3; }
        default int extra() { return 40; }
    }

    // Whether the last call reached the handler as a method of the current Api class.
    public static boolean currentInterface;
    public static int calls;

    public static Api create() {
        InvocationHandler handler = (proxy, method, arguments) -> {
            calls++;
            currentInterface = method.getDeclaringClass() == Api.class;
            switch (method.getName()) {
                case "get": return 5;
                case "removed": return 6;
                case "twice": return 77;
                default: return 8;
            }
        };
        return (Api) Proxy.newProxyInstance(Api.class.getClassLoader(), new Class<?>[]{Api.class}, handler);
    }

    public static int get(Api api) { return api.get(); }
    public static int removed(Api api) { return 0; }
    // Calls through Api `times` times; `get` turns hot and is compiled.
    public static int sum(Api api, int times) {
        int sum = 0;
        for (int i = 0; i < times; i++) sum += get(api);
        return sum;
    }

    private ProxyFixture() {}
}
