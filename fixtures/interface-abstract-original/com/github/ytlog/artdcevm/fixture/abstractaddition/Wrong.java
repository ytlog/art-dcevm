package com.github.ytlog.artdcevm.fixture.abstractaddition;

public class Wrong implements Contract { public String required() { return "unused"; } public String required(int value) { return "wrong"; } public Object requiredObject() { return "wrong-return"; } }
