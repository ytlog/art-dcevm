package com.github.ytlog.artdcevm.fixture;
public class HierarchyInserted extends HierarchyBase {
 public static int constructions;
 public double added = 2.5; public Object addedRef = new Object(); public int value = 39;
 public HierarchyInserted() { constructions++; }
 @Override public int inherited() { return base + 40; }
}
