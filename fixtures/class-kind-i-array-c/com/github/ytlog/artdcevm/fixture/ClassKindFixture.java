package com.github.ytlog.artdcevm.fixture;

public final class ClassKindFixture {
    public static class InterfaceTarget {
        public int value() { return 7; }
        public static InterfaceTarget[] array() { return new InterfaceTarget[]{new InterfaceTarget()}; }
    }
}
