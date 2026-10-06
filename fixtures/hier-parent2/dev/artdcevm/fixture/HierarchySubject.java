package dev.artdcevm.fixture;
public class HierarchySubject extends HierarchyOuter implements HierarchyMore {
 public int value = 37; public Object ownRef; public static int counter = 19;
 public int parentBatchValue() { return super.batchValue(); }
 public int compute() { return value + base + 4; }
 public int tag() { return value + 4; }
 public int throughSuper() { return super.inherited(); }
 public long active() { DeletionControl.awaitRelease(); return root + value + 4; }
}
