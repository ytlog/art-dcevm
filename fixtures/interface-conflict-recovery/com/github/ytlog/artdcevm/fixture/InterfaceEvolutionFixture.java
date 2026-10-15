package com.github.ytlog.artdcevm.fixture;
public final class InterfaceEvolutionFixture {
 public interface Left { default int left() { return 13; } }
 public static class ConflictBase { public int marker = 29; public int base() { return 32; } public int added() { return 33; } }
}
