package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

/** Independent public non-field handle with verified original array access. */
public final class VarHandleClassOracle {
    private VarHandleClassOracle() {}
    public static VarHandle create() {
        VarHandle handle = MethodHandles.arrayElementVarHandle(int[].class);
        int[] array = {19, 23};
        if ((int) handle.get(array, 0) != 19 || (int) handle.get(array, 1) != 23)
            throw new IllegalStateException("Original array VarHandle read differs");
        handle.set(array, 0, 31);
        if ((int) handle.get(array, 0) != 31 || array[0] != 31 || array[1] != 23)
            throw new IllegalStateException("Original array VarHandle write differs");
        return handle;
    }
}
