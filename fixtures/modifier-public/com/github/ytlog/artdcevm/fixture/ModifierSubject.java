package com.github.ytlog.artdcevm.fixture;

public class ModifierSubject {
    public int value = 41;
    public int packageValue = 5;
    public int compute() { return value + 2; }
    public int packageCompute() { return packageValue + 8; }
    public int throughPublic() { return compute(); }
    public static int staticCompute() { return 19; }
    public int finalizable() { return 29; }
}
