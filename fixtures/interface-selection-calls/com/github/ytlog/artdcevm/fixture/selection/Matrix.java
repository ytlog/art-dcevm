package com.github.ytlog.artdcevm.fixture.selection;
public final class Matrix {
    public interface Contract { default int stable() { return 19; } default int select() { return 7; } }
    public static class Plain implements Contract { public int oldCall() { return 0; } }
    public static class OwnPrivate implements Contract { public int oldCall() { return 0; } }
    public static class PrivateChild implements Contract { public int oldCall() { return 0; } }
    public static class PrivateGrandchild implements Contract { public int oldCall() { return 0; } }
    public static class OwnPackage implements Contract { public int oldCall() { return 0; } }
    public static class PackageChild implements Contract { public int oldCall() { return 0; } }
    public static class OwnProtected implements Contract { public int oldCall() { return 0; } }
    public static class ProtectedChild implements Contract { public int oldCall() { return 0; } }
    public static class OtherPackageChild implements Contract { public int oldCall() { return 0; } }
    public static class PrivatePublic implements Contract { public int oldCall() { return 0; } }
    public static class PackagePublic implements Contract { public int oldCall() { return 0; } }
    public static class ProtectedPublic implements Contract { public int oldCall() { return 0; } }
    public static class OtherPackagePublic implements Contract { public int oldCall() { return 0; } }
    public static class OwnPrivateLate implements Contract { public int oldCall() { return 0; } }
    public static class PrivateLate implements Contract { public int oldCall() { return 0; } }
    public static class PackageLate implements Contract { public int oldCall() { return 0; } }
    public static class ProtectedLate implements Contract { public int oldCall() { return 0; } }
    public static class OtherPackageLate implements Contract { public int oldCall() { return 0; } }
    public static class PublicLate implements Contract { public int oldCall() { return 0; } }
    public static class RedeclaredPrivate implements Contract { public int oldCall() { return 0; } }
    public static class OwnOnInherited implements Contract { public int oldCall() { return 0; } }
    public static class OwnOnPlain implements Contract { public int oldCall() { return 0; } }
}
