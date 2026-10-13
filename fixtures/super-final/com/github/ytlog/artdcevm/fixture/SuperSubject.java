package com.github.ytlog.artdcevm.fixture;

/** Changes its superclass through redefinition (fixtures/super-*). */
public class SuperSubject extends SuperFinal {
    public int own = 7;
    public int value() { return own + baseValue; }
}
