package com.github.ytlog.artdcevm.fixture.other;

/** A package-private method that subclasses in other packages do not inherit. */
public class OtherPackageBase {
    int shadow() { return 92; }
    public int oldCall() { return shadow(); }
}
