package dev.artdcevm.fixture;
public final class StorageOldCaller {
    public static int valueHot(StorageSubject s) { return s.value; }
    public static void setValueHot(StorageSubject s, int x) { s.value = x; }
    public static long countHot(StorageSubject s) { return StorageSubject.count; }
    public static void setCountHot(StorageSubject s, long x) { StorageSubject.count = x; }
    public static Object referenceHot(StorageSubject s) { return s.reference; }
    public static void setReferenceHot(StorageSubject s, Object x) { s.reference = x; }
    public static Object sharedHot(StorageSubject s) { return StorageSubject.shared; }
    public static void setSharedHot(StorageSubject s, Object x) { StorageSubject.shared = x; }
    public static int changeHot(StorageSubject s, int x) { return s.change(x); }
    public static long alternateHot(StorageSubject s, long x) { return StorageSubject.alternate(x); }
    public static int valueCold(StorageSubject s) { return s.value; }
    public static void setValueCold(StorageSubject s, int x) { s.value = x; }
    public static long countCold(StorageSubject s) { return StorageSubject.count; }
    public static void setCountCold(StorageSubject s, long x) { StorageSubject.count = x; }
    public static Object referenceCold(StorageSubject s) { return s.reference; }
    public static void setReferenceCold(StorageSubject s, Object x) { s.reference = x; }
    public static Object sharedCold(StorageSubject s) { return StorageSubject.shared; }
    public static void setSharedCold(StorageSubject s, Object x) { StorageSubject.shared = x; }
    public static int changeCold(StorageSubject s, int x) { return s.change(x); }
    public static long alternateCold(StorageSubject s, long x) { return StorageSubject.alternate(x); }
}
