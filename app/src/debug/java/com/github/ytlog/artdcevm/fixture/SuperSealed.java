package com.github.ytlog.artdcevm.fixture;

/** Declares value() final, which SuperSubject overrides. */
public class SuperSealed extends SuperBase {
    public final int value() { return 0; }
}
