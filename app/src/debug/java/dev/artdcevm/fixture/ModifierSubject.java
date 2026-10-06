package dev.artdcevm.fixture;

public class ModifierSubject {
    protected int value = 41;
    int packageValue = 5;
    protected int compute() { return value + 1; }
    int packageCompute() { return packageValue + 7; }
    public int throughPublic() { return compute(); }
    public static int staticCompute() { return 19; }
    public int finalizable() { return 29; }
}
