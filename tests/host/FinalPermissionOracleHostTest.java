package com.github.ytlog.artdcevm.internal;

/** Normal JVM checks the real fixture's mutable cached MH/VH and writer signatures. */
public final class FinalPermissionOracleHostTest {
    public static void main(String[] args) throws Throwable {
        FinalPermissionOracle.Target target = FinalPermissionOracle.create();
        FinalPermissionOracle.Handles handles = FinalPermissionOracle.prepareHandles();
        if (FinalPermissionOracle.checkHandles(handles, target, false, 41) != 35 || target.instanceValue != 42)
            throw new AssertionError("mutable cached instance handles or reflection");
        if (FinalPermissionOracle.checkHandles(handles, target, true, 43) != 35 || FinalPermissionOracle.Target.staticValue != 44)
            throw new AssertionError("mutable cached static handles or reflection");
        FinalPermissionOracle.write(target, 55, false);
        FinalPermissionOracle.write(target, 66, true);
        if (target.instanceValue != 55 || FinalPermissionOracle.Target.staticValue != 66)
            throw new AssertionError("foreign bytecode writer signatures");
        System.out.println("Actual Java fixture: mutable MH/VH getters/setters, reflection and foreign instance/static bytecode passed");
    }
}
