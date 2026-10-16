package com.github.ytlog.artdcevm.fixture.abstractaddition;

public class Implementation implements Contract {
 public static Implementation singleton = new Implementation(); public static int counter = 7;
 public int marker = 0;
 public String required() { return "new"; }
 public String version() { return "new"; }
}
