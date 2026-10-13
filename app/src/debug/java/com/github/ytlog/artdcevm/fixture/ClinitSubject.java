package com.github.ytlog.artdcevm.fixture;

/** fixtures/clinit-delete drops the static initializer. */
public class ClinitSubject {
    public static int value = compute();
    public static int compute() { return 41; }
}
