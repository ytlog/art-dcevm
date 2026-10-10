package com.github.ytlog.artdcevm.internal;
/** Library-owned source invocation witnesses. DEX opcodes are verified at build. */
public final class ReceiverSourceFixture {
    public interface Contract {
        int interfaceSmall(int a);
        int interfaceRange(int a, int b, int c, int d, int e, int f);
    }
    public static class Base {
        public int virtualSmall(int a) { return a + 10; }
        public int virtualRange(int a, int b, int c, int d, int e, int f) { return a+b+c+d+e+f+10; }
        public int superSmall(int a) { return a + 20; }
        public int superRange(int a, int b, int c, int d, int e, int f) { return a+b+c+d+e+f+20; }
    }
    public static final class Receiver extends Base implements Contract {
        public int interfaceSmall(int a) { return a + 30; }
        public int interfaceRange(int a, int b, int c, int d, int e, int f) { return a+b+c+d+e+f+30; }
        private int directSmall(int a) { return a + 40; }
        private int directRange(int a, int b, int c, int d, int e, int f) { return a+b+c+d+e+f+40; }
        public int callDirectSmall() { return directSmall(1); }
        public int callDirectRange() { return directRange(1,2,3,4,5,6); }
        public int callSuperSmall() { return super.superSmall(1); }
        public int callSuperRange() { return super.superRange(1,2,3,4,5,6); }
    }
    public static int staticSmall(int a) { return a + 50; }
    public static int staticRange(int a,int b,int c,int d,int e,int f) { return a+b+c+d+e+f+50; }
    public static int callVirtualSmall(Base r) { return r.virtualSmall(1); }
    public static int callVirtualRange(Base r) { return r.virtualRange(1,2,3,4,5,6); }
    public static int callInterfaceSmall(Contract r) { return r.interfaceSmall(1); }
    public static int callInterfaceRange(Contract r) { return r.interfaceRange(1,2,3,4,5,6); }
    public static int callStaticSmall() { return staticSmall(1); }
    public static int callStaticRange() { return staticRange(1,2,3,4,5,6); }
}
