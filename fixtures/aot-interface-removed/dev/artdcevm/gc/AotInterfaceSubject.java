package dev.artdcevm.gc;

public class AotInterfaceSubject {
    public int own = 17;
    public int tag() { return own + 2; }
    public int defaultValue() { return own + 71; }
}
