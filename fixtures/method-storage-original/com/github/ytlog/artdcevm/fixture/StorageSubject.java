package com.github.ytlog.artdcevm.fixture;

/**
 * fixtures/method-storage turns staticValue into an instance method and
 * virtualValue into a static one; method-storage-original turns them back.
 * The method names match the JNI ID helpers of the test client.
 */
public class StorageSubject {
    public int value = 5;
    public static int base = 30;
    private int directValue() { return value + 1; }
    public static int staticValue() { return base + 2; }
    public int virtualValue() { return value + 3; }
    public int signatureValue(int delta) { return value + delta; }
    public int combined() { return staticValue() + virtualValue(); }
}
