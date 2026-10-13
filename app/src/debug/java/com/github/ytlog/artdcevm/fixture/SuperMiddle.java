package com.github.ytlog.artdcevm.fixture;

/** Inserted between SuperSubject and SuperBase by fixtures/super-widen. */
public class SuperMiddle extends SuperBase {
    public int middleValue = 11;
    public int middle() { return 40 + middleValue; }
    @Override public String describe() { return "middle"; }
}
