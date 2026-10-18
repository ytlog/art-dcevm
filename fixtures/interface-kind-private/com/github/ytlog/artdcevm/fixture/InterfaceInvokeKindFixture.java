package com.github.ytlog.artdcevm.fixture;

/** Interface methods that change between default, static and private. */
public final class InterfaceInvokeKindFixture {
    private InterfaceInvokeKindFixture() {}

    public interface Contract {
        private int value() { return 31; }
        default int selfValue() { return value(); }
        static int twice(int x) { return 2 * x; }
        private int helper() { return 6; }
        default int useHelper() { return helper() + 1; }
    }
    public static class Impl implements Contract {
        public int marker = 3;
    }
    public static final class Caller {
        private Caller() {}
        public static int interfaceCall(Contract contract) { return contract.value(); }
        public static int call(Impl impl) { return ((Contract) impl).value(); }
    }
}
