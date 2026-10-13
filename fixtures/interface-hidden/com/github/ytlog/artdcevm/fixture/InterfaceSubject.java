package com.github.ytlog.artdcevm.fixture;

/** Gains interfaces through redefinition (fixtures/interface-*). */
public class InterfaceSubject implements com.github.ytlog.artdcevm.fixture.other.HiddenContract {
    public int value = 50;
    public int contract() { return value + 1; }
    public int getAsInt() { return value + 2; }
}
