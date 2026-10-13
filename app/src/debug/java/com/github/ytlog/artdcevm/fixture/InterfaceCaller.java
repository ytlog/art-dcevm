package com.github.ytlog.artdcevm.fixture;

/** Bytecode access to AddedContract; call only after the interface is added. */
public final class InterfaceCaller {
    private InterfaceCaller() {}
    public static boolean isContract(Object value) { return value instanceof AddedContract; }
    public static int contractTwice(Object value) { return ((AddedContract) value).contractTwice(); }
}
