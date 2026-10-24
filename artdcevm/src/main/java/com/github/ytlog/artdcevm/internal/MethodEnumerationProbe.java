package com.github.ytlog.artdcevm.internal;

import java.lang.reflect.Method;

/** Prove live Class.getMethods collection and actual reflected calls. */
public final class MethodEnumerationProbe {
    private MethodEnumerationProbe() { }
    public interface Root { Object value(); default int rank() { return 1; } }
    public interface Child extends Root {
        @Override default String value() { return "interface"; }
        @Override default int rank() { return 7; }
    }
    public interface Leaf extends Child { }
    public static class Receiver implements Leaf { }
    public static class Own implements Leaf {
        @Override public String value() { return "class"; }
        @Override public int rank() { return 9; }
    }
    public interface StaticRoot { static int hidden() { return 13; } }
    public interface StaticMixed extends StaticRoot {
        static int mixed() { return 17; }
        default int live() { return 19; }
    }
    public interface StaticLeaf extends StaticMixed { }
    public static class StaticReceiver implements StaticLeaf { }
    public static class StaticOwn extends StaticReceiver { public static int hidden() { return 23; } }
    public static class StaticSubclass extends StaticOwn { }
    public interface BootstrapLeaf extends java.util.Comparator<String>, java.util.function.Function<String,String> { }
    public static class BootstrapReceiver implements BootstrapLeaf {
        public int compare(String a, String b) { return a.compareTo(b); }
        public String apply(String a) { return a; }
    }
    public static void runBootstrapStatic() throws Exception {
        if (java.util.Comparator.class.getClassLoader()!=Object.class.getClassLoader()
                || java.util.function.Function.class.getClassLoader()!=Object.class.getClassLoader())
            throw new AssertionError("standard interfaces are not bootstrap declarations");
        for (Class<?> owner : new Class<?>[]{BootstrapLeaf.class,BootstrapReceiver.class}) {
            int reverse = 0, then = 0;
            for (Method method : owner.getMethods()) {
                if (method.getDeclaringClass().getClassLoader()==Object.class.getClassLoader()
                        && method.getDeclaringClass().isInterface()
                        && java.lang.reflect.Modifier.isStatic(method.getModifiers()))
                    throw new AssertionError("inherited bootstrap static");
                if (method.getName().equals("reversed")) {
                    if (method.getDeclaringClass()!=java.util.Comparator.class
                            || ((java.util.Comparator<String>)method.invoke(new BootstrapReceiver())).compare("a","b")!=1)
                        throw new AssertionError("bootstrap default changed");
                    ++reverse;
                }
                if (method.getName().equals("andThen")) {
                    Object value = ((java.util.function.Function<String,String>)method.invoke(new BootstrapReceiver(),
                            java.util.function.Function.identity())).apply("value");
                    if (!"value".equals(value)) throw new AssertionError("bootstrap function changed");
                    ++then;
                }
            }
            if (reverse!=1 || then!=1) throw new AssertionError("bootstrap instance declaration lost");
        }
        int own = 0;
        for (Method method : java.util.Comparator.class.getMethods()) {
            if (method.getName().equals("naturalOrder")
                    && ((java.util.Comparator<String>)method.invoke(null)).compare("a","b")==-1) ++own;
        }
        Object token = new Object();
        if (own!=1 || ((java.util.function.Function<Object,Object>)java.util.function.Function.class
                .getDeclaredMethod("identity").invoke(null)).apply(token)!=token)
            throw new AssertionError("bootstrap own declarations changed");
    }
    public static void runStatic() throws Exception {
        for (Class<?> owner : new Class<?>[]{StaticLeaf.class, StaticReceiver.class, StaticOwn.class, StaticSubclass.class}) {
            int own = 0, live = 0;
            boolean hasOwn = owner == StaticOwn.class || owner == StaticSubclass.class;
            for (Method method : owner.getMethods()) {
                if (method.getName().equals("mixed")) throw new AssertionError("inherited interface static");
                if (method.getName().equals("hidden")) {
                    if (!hasOwn || method.getDeclaringClass() != StaticOwn.class
                            || !Integer.valueOf(23).equals(method.invoke(null))) throw new AssertionError("class static changed");
                    ++own;
                }
                if (method.getName().equals("live")) {
                    if (!Integer.valueOf(19).equals(method.invoke(new StaticReceiver()))) throw new AssertionError("instance method lost");
                    ++live;
                }
            }
            if (own != (hasOwn ? 1 : 0) || live != 1) throw new AssertionError("incomplete static filtering");
        }
        int declarations = 0;
        for (Method method : StaticRoot.class.getMethods()) {
            if (method.getName().equals("hidden") && method.getDeclaringClass() == StaticRoot.class
                    && Integer.valueOf(13).equals(method.invoke(null))) ++declarations;
        }
        if (declarations != 1 || !Integer.valueOf(13).equals(StaticRoot.class.getDeclaredMethod("hidden").invoke(null))
                || !Integer.valueOf(17).equals(StaticMixed.class.getDeclaredMethod("mixed").invoke(null)))
            throw new AssertionError("own static declaration lost");
    }
    public static void run() throws Exception {
        for (Class<?> owner : new Class<?>[]{Leaf.class, Receiver.class, Own.class}) {
            boolean own = owner == Own.class;
            Object receiver = own ? new Own() : new Receiver();
            Class<?> expectedOwner = own ? Own.class : Child.class;
            int bridges = 0, narrow = 0, ranks = 0;
            for (Method method : owner.getMethods()) {
                if (method.getName().equals("value")) {
                    if (method.getDeclaringClass() != expectedOwner
                            || !(own ? "class" : "interface").equals(method.invoke(receiver)))
                        throw new AssertionError("enumeration chose an ancestor");
                    if (method.getReturnType() == Object.class && method.isBridge()) ++bridges;
                    if (method.getReturnType() == String.class && !method.isBridge()) ++narrow;
                } else if (method.getName().equals("rank")) {
                    if (method.getDeclaringClass() != expectedOwner || !Integer.valueOf(own ? 9 : 7).equals(method.invoke(receiver)))
                        throw new AssertionError("enumeration changed class or interface priority");
                    ++ranks;
                }
            }
            if (bridges != 1 || narrow != 1 || ranks != 1) throw new AssertionError("incomplete enumeration");
        }
        if (Leaf.class.getDeclaredMethods().length != 0) throw new AssertionError("declared enumeration changed");
    }
}
