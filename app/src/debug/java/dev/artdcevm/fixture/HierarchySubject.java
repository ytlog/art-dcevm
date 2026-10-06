package dev.artdcevm.fixture;
public class HierarchySubject extends HierarchyBase  {
 public int value = 37; public Object ownRef; public static int counter = 19;
 public int compute() { return value + base + 1; }
 public int tag() { return value + 1; }
 public int throughSuper() { return super.inherited(); }
 public long active() { DeletionControl.awaitRelease(); return root + value + 1; }
}
