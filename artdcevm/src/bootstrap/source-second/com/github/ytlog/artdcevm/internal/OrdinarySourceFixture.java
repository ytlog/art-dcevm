package com.github.ytlog.artdcevm.internal;
/** Library-owned ordinary ART lifecycle oracle; never a user class. */
public class OrdinarySourceFixture {
    public int value;
    public int compute() { return value + 1; }
}
