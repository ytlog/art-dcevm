package com.github.ytlog.artdcevm.fixture;

/** Compiled against the original StorageSubject and never redefined. */
public final class StorageCaller {
    private StorageCaller() {}
    public static int callStatic() { return StorageSubject.staticValue(); }
    public static int callVirtual(StorageSubject subject) { return subject.virtualValue(); }
}
