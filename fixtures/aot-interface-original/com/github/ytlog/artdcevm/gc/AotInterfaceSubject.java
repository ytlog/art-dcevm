package com.github.ytlog.artdcevm.gc;

public class AotInterfaceSubject implements AotInterfaceTag {
    public int own = 17;
    @Override public int tag() { return own + 1; }
}
