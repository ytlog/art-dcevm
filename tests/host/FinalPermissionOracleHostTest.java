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
        FinalPermissionOracle.write(target, 55, null, false, false);
        FinalPermissionOracle.write(target, 66, null, true, false);
        if (target.instanceValue != 55 || FinalPermissionOracle.Target.staticValue != 66)
            throw new AssertionError("foreign bytecode writer signatures");
        FinalPermissionOracle.write(target, 0, target, false, true);
        FinalPermissionOracle.write(target, 0, target, true, true);
        if (FinalPermissionOracle.checkReferenceHandles(handles, target, false, target) != 35 || target.instanceReference != handles)
            throw new AssertionError("mutable cached reference instance handles or reflection");
        if (FinalPermissionOracle.checkReferenceHandles(handles, target, true, target) != 35 || FinalPermissionOracle.Target.staticReference != handles)
            throw new AssertionError("mutable cached reference static handles or reflection");
        System.out.println("Actual Java fixture: mutable MH/VH getters/setters, reflection and foreign instance/static bytecode passed");
    }
}
