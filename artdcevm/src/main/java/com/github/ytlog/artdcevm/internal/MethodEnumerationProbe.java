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
