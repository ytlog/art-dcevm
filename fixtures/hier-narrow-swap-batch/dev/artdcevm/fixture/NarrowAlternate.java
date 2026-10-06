package dev.artdcevm.fixture;

public class NarrowAlternate {
    public int alternate = 29;
    public int revision = 5;
    public int inherited() { return alternate + revision + 10; }
}
