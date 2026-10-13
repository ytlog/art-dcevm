package com.github.ytlog.artdcevm.fixture.other;

public class ContractHost {
    /** Public only for compilation; the app interface is protected. */
    public interface NestedContract {
        default int nested() { return 7; }
    }
}
