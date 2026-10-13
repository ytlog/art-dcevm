package com.github.ytlog.artdcevm.fixture;

/** fixtures/constructor-delete removes ConstructorSubject(int). */
public class ConstructorSubject {
    public int value;
    public ConstructorSubject() { value = 1; }
    public ConstructorSubject(int value) { this.value = value; }
}
