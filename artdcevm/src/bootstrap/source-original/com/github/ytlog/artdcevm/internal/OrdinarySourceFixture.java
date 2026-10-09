package com.github.ytlog.artdcevm.internal;
/** Library-owned ordinary ART lifecycle oracle; never a user class. */
public class OrdinarySourceFixture {
    public int value;
    public int compute() { return value + 1; }
    public int first() { return value + 2; }
    public int second() { return value + 3; }
}
