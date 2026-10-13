package com.github.ytlog.artdcevm.fixture.other;

public class ContractHost {
    /** InnerClass flags say protected; the ClassDef flags are public. */
    protected interface NestedContract {
        default int nested() { return 7; }
    }
}
