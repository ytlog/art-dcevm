package com.github.ytlog.artdcevm.fixture.selection;

/** Loaded independently to preserve runtime-package and resolution boundaries. */
public final class Matrix {
    public interface Contract { default int stable() { return 19; } }
    public interface Parent { default int select() { return 17; } }
    public static class Plain implements Contract { public int oldCall() { return 100; } }
    public static class OwnPrivate implements Contract {
        private int select() { return 101; }
        public int oldCall() { return select(); }
    }
    public static class PrivateBase {
        private int select() { return 102; }
        public int oldCall() { return select(); }
    }
    public static class PrivateChild extends PrivateBase implements Contract {}
    public static class PrivateGrandchild extends PrivateChild {}
    public static class OwnPackage implements Contract {
        int select() { return 111; }
        public int oldCall() { return select(); }
    }
    public static class PackageBase {
        int select() { return 112; }
        public int oldCall() { return select(); }
    }
    public static class PackageChild extends PackageBase implements Contract {}
    public static class OwnProtected implements Contract {
        protected int select() { return 121; }
        public int oldCall() { return select(); }
    }
    public static class ProtectedBase {
        protected int select() { return 122; }
        public int oldCall() { return select(); }
    }
    public static class ProtectedChild extends ProtectedBase implements Contract {}
    public static class OtherPackageChild extends com.github.ytlog.artdcevm.fixture.selection.other.PackageBase implements Contract {}
    public static class PrivatePublic extends PrivateChild { public int select() { return 141; } }
    public static class PackagePublic extends PackageChild { public int select() { return 142; } }
    public static class ProtectedPublic extends ProtectedChild { public int select() { return 143; } }
    public static class OtherPackagePublic extends OtherPackageChild { public int select() { return 144; } }
    public static class RedeclaredPrivate extends PrivateChild {
        private int select() { return 104; }
        public int oldCall() { return select(); }
    }
    public static class OwnOnInherited extends OwnPrivate {
        private int select() { return 105; }
        public int oldCall() { return select(); }
    }
    public static class OwnOnPlain extends Plain {
        private int select() { return 106; }
        public int oldCall() { return select(); }
    }
    public static class OwnPrivateLate implements Contract {
        private int select() { return 103; }
        public int oldCall() { return select(); }
    }
    public static class PrivateLate extends PrivateChild {}
    public static class PackageLate extends PackageChild {}
    public static class ProtectedLate extends ProtectedChild {}
    public static class OtherPackageLate extends OtherPackageChild {}
    public static class PublicLate extends ProtectedChild { public int select() { return 145; } }
}
