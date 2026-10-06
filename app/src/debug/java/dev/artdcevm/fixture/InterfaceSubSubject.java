package dev.artdcevm.fixture;

public class InterfaceSubSubject implements InterfaceSubTag {
    public int own = 25;
    @Override public int tag() { return own + 1; }
}
