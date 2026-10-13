package com.github.ytlog.artdcevm.fixture;

/** Referenced only by InterfaceCaller, so it stays unloaded until a redefinition adds it. */
public interface AddedContract {
    int contract();
    default int contractTwice() { return contract() * 2; }
}
