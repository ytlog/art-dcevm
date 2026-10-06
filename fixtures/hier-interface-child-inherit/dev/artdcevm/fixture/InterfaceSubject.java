package dev.artdcevm.fixture;

public class InterfaceSubject implements InterfaceTag {
    public int own = 17;
    @Override public int tag() { return own + 3; }
}
