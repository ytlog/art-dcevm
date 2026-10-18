package com.github.ytlog.artdcevm.internal;

/** Enters a genuine native frame for the runtime Thread probe while linking. */
public final class InterfaceLinkProbe {
    private InterfaceLinkProbe() {}
    public static native void probe();
}
