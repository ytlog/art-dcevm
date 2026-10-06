package dev.artdcevm.fixture;

public class InterfacePeer implements InterfaceTag {
    public int own = 23;
    @Override public int tag() { return own + 5; }
}
