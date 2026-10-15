package com.github.ytlog.artdcevm.fixture;

public final class MultiInterfaceFixture {
    public interface Left {
        default int leftValue() { return 11; }
    }
    public interface Right {
        default int rightValue() { return 21; }
    }
    public interface Parent {
        default int parentValue() { return 31; }
    }
    public interface Child extends Parent {
        default int childValue() { return 41; }
    }
    public static class LeftImpl implements Left { public int marker = 17; }
    public static class RightImpl implements Right { public int marker = 19; }
    public static class FamilyImpl implements Child { public int marker = 23; }
    public static class FamilyChild extends FamilyImpl { public int childMarker = 29; }
    public static class SharedImpl implements Left, Right { public int marker = 31; }
}
