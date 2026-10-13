package com.github.ytlog.artdcevm.fixture;
public class FieldSubject {
    public int keep = 41;
    public static int staticKeep = 42;
    public boolean z = true;
    public byte b = 3;
    public char c = 65;
    public short s = 5;
    public int i = 7;
    public long j = 9_000_000_000L;
    public float f = 1.25f;
    public double d = 2.5;
    public Object o = new Object();
    public static boolean sz = true;
    public static byte sb = 3;
    public static char sc = 65;
    public static short ss = 5;
    public static int si = 7;
    public static long sj = 9_000_000_000L;
    public static float sf = 1.25f;
    public static double sd = 2.5;
    public static Object so = new Object();
    public int compute() { return keep; }
}
