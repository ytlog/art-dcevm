package com.github.ytlog.artdcevm.fixture;

import java.lang.reflect.Method;
import java.util.function.IntSupplier;

/**
 * Keeps the interface fixtures in Object-typed fields and observes them through
 * methods that return only values, so no caller frame holds an instance when an
 * interface removal is checked against live references.
 */
public final class InterfaceHolder {
    private static Object subject;
    private static Object child;

    private InterfaceHolder() {}

    public static void create() {
        subject = new InterfaceSubject();
        child = new InterfaceChild();
    }

    static Object subject() { return subject; }

    public static int contract() { return ((InterfaceSubject) subject).contract(); }
    public static boolean isContract(boolean ofChild) { return InterfaceCaller.isContract(ofChild ? child : subject); }
    public static int contractTwice(boolean ofChild) { return InterfaceCaller.contractTwice(ofChild ? child : subject); }
    public static boolean newIsContract() { return InterfaceCaller.isContract(new InterfaceSubject()); }
    public static int newContractTwice() { return InterfaceCaller.contractTwice(new InterfaceSubject()); }
    public static int intSupplier() { return ((IntSupplier) subject).getAsInt(); }

    /** The default method of the third interface, invoked reflectively. */
    public static Object nested(boolean ofChild) throws Exception {
        Method method = InterfaceSubject.class.getInterfaces()[2].getMethod("nested");
        method.setAccessible(true);
        return method.invoke(ofChild ? child : subject);
    }
}
