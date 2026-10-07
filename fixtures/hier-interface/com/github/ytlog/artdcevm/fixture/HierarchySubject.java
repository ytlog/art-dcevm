package com.github.ytlog.artdcevm.fixture;
public class HierarchySubject extends HierarchyBase implements HierarchyTag {
 public int value = 37; public Object ownRef; public static int counter = 19;
 public int compute() { return value + base + 2; }
 public int tag() { return value + 2; }
 public int throughSuper() { return super.inherited(); }
 public long active() { DeletionControl.awaitRelease(); return root + value + 2; }
}
