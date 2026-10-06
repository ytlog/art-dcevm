package dev.artdcevm.fixture;
public class HierarchyCaller {
 public static int hot(HierarchySubject object) { return object.base; }
 public static int cold(HierarchySubject object) { return object.base; }
 public static boolean inserted(Object object) { return object instanceof HierarchyInserted; }
 public static boolean more(Object object) { return object instanceof HierarchyMore; }
}
