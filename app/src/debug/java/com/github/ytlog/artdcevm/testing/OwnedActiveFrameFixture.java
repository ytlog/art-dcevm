package com.github.ytlog.artdcevm.testing;

/** Owned carrier and two inlinees; each pair remains live across the native gate. */
public final class OwnedActiveFrameFixture {
    public static volatile Object retained0,retained1,retained2;
    public static volatile int retainedInt0,retainedInt1,retainedInt2;
    public static native void awaitGate();
    public int marker() { return 7; } // independent instance method identity oracle
    public static int carrier(int a,Object ar,int b,Object br,int c,Object cr) {
        int nested=inlinee1(a,ar,b,br,c,cr);
        retained0=ar;retainedInt0=a;
        return nested+a+System.identityHashCode(ar);
    }
    public static int inlinee1(int a,Object ar,int b,Object br,int c,Object cr) {
        int nested=inlinee2(a,ar,b,br,c,cr);
        retained1=br;retainedInt1=b;
        return nested+b+System.identityHashCode(br);
    }
    public static int inlinee2(int a,Object ar,int b,Object br,int c,Object cr) {
        awaitGate();
        retained2=cr;retainedInt2=c;
        return c+System.identityHashCode(cr);
    }
    private OwnedActiveFrameFixture() { }
}
