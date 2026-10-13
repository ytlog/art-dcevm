package com.github.ytlog.artdcevm.fixture;

/** Gains interfaces through redefinition (fixtures/interface-*). */
public class InterfaceSubject implements AddedContract, java.util.function.IntSupplier,
        com.github.ytlog.artdcevm.fixture.other.ContractHost.NestedContract {
    public int value = 50;
    public int contract() { return value + 1; }
    public int getAsInt() { return value + 2; }
}
