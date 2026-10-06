package dev.artdcevm.fixture;
public final class StorageNewCaller {
    public static int valueHot(StorageSubject s) { return StorageSubject.value; }
    public static void setValueHot(StorageSubject s, int x) { StorageSubject.value = x; }
    public static long countHot(StorageSubject s) { return s.count; }
    public static void setCountHot(StorageSubject s, long x) { s.count = x; }
    public static Object referenceHot(StorageSubject s) { return StorageSubject.reference; }
    public static void setReferenceHot(StorageSubject s, Object x) { StorageSubject.reference = x; }
    public static Object sharedHot(StorageSubject s) { return s.shared; }
    public static void setSharedHot(StorageSubject s, Object x) { s.shared = x; }
    public static int changeHot(StorageSubject s, int x) { return StorageSubject.change(x); }
    public static long alternateHot(StorageSubject s, long x) { return s.alternate(x); }
    public static int valueCold(StorageSubject s) { return StorageSubject.value; }
    public static void setValueCold(StorageSubject s, int x) { StorageSubject.value = x; }
    public static long countCold(StorageSubject s) { return s.count; }
    public static void setCountCold(StorageSubject s, long x) { s.count = x; }
    public static Object referenceCold(StorageSubject s) { return StorageSubject.reference; }
    public static void setReferenceCold(StorageSubject s, Object x) { StorageSubject.reference = x; }
    public static Object sharedCold(StorageSubject s) { return s.shared; }
    public static void setSharedCold(StorageSubject s, Object x) { s.shared = x; }
    public static int changeCold(StorageSubject s, int x) { return StorageSubject.change(x); }
    public static long alternateCold(StorageSubject s, long x) { return s.alternate(x); }
}
