package com.github.ytlog.artdcevm.fixture;

public class InterfaceSubject {
    public int own = 17;
    public int tag() { return own + 2; }
    public int defaultValue() { return own + 71; }
}
