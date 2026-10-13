package com.github.ytlog.artdcevm.fixture;

/** value() is not final only for compilation; the app method is final. */
public class SuperSealed extends SuperBase {
    public int value() { return 0; }
}
