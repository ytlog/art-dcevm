package com.github.ytlog.artdcevm.fixture;
/** Retained, unchanged typed cells used to distinguish legal from illegal migration. */
public final class NarrowStorage {
    public Object legal;
    public Object[] legalArray;
    public NarrowParent ancestor;
    public static NarrowParent staticAncestor;
    public NarrowParent[] ancestorArray;
}
