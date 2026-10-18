package com.github.ytlog.artdcevm.fixture;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Proxy;

/** A java.lang.reflect.Proxy implementation of a redefined interface (fixtures/proxy-api-*). */
public final class ProxyFixture {
    public interface Api {
        int get();
        int removed();
        default int twice() { return get() * 2; }
    }

    // Not a parent of Api until fixtures/proxy-parent-add adds it.
    public interface Parent {
        default int parentDefault() { return 41; }
        int parentAbstract();
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

    public static boolean isParent(Object object) { return object instanceof Parent; }
    // A checkcast and invoke-interface through Parent from verified code.
    public static int parentDefault(Object object) { return ((Parent) object).parentDefault(); }

    public static int get(Api api) { return api.get(); }
    public static int removed(Api api) { return api.removed(); }
    // Calls through Api `times` times; `get` turns hot and is compiled.
    public static int sum(Api api, int times) {
        int sum = 0;
        for (int i = 0; i < times; i++) sum += get(api);
        return sum;
    }

    private ProxyFixture() {}
}
