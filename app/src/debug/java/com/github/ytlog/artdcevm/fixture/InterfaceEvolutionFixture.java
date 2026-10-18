package com.github.ytlog.artdcevm.fixture;

/** Independent types keep interface evolution scenarios from changing each other. */
public final class InterfaceEvolutionFixture {
    public interface Contract {
        private int directValue() { return 61; }
        static int staticValue() { return 82; }
        default int virtualValue() { return 63; }
        default int signatureValue(int value) { return 60 + value; }
        default int throughDirect() { return directValue(); }
        default int throughStatic() { return staticValue(); }

    }
    public interface Deletion {
        private int directValue() { return 61; }
        private static int staticValue() { return 82; }
        default int virtualValue() { return 63; }
        default int signatureValue(int value) { return 60 + value; }
        default int throughDirect() { return directValue(); }
        default int throughStatic() { return staticValue(); }

    }
    public static class DeleteImpl implements Deletion {}
    public static class Implementation implements Contract { public int marker = 17; }
    public interface Parent { default int inherited() { return 20; } }
    public interface Root extends Parent {
        default int value() { return 1; }

    }
    public interface Child extends Root { default int added() { return 70; } }
    public static class RootImpl implements Root { public int marker = 19; }
    public static class ChildImpl implements Child { public int marker = 23; }
    public static class PublicBase { public int added() { return 80; } }
    public static class PublicImpl extends PublicBase implements Root {}
    public interface AbstractParent { int miranda(); }
    public interface MirandaRoot extends AbstractParent {
        default int stable() { return 7; }

    }
    public static abstract class MirandaBase implements MirandaRoot {}
    public static class MirandaImpl extends MirandaBase {
        public int miranda() { return 9; }
    }
    public interface Left { default int left() { return 11; } }
    public interface Right { default int conflict() { return 21; } }
    public static class ConflictBase { public int marker = 29; public int base() { return 30; } }
    public static class Diamond extends ConflictBase implements Left, Right {}
    public interface HiddenParent { default int clash() { return 31; } }
    public interface Hiding extends HiddenParent { }
    public static class HidingImpl implements Hiding {}
    public interface ObjectHiding { default int stable() { return 41; } }
    public static class ObjectImpl implements ObjectHiding {}
    public interface StaticShadow { default int stable() { return 51; } }
    public static class StaticBase { public static int shadow() { return 52; } }
    public static class StaticImpl extends StaticBase implements StaticShadow {}
    public interface PrivateShadow { default int stable() { return 61; } }
    public static class PrivateImpl implements PrivateShadow {
        private int shadow() { return 62; }
        public int oldCall() { return shadow(); }
    }
    public interface ProtectedShadow { default int stable() { return 71; } }
    public static class ProtectedBase {
        protected int shadow() { return 72; }
        public int oldCall() { return shadow(); }
    }
    public static class ProtectedImpl extends ProtectedBase implements ProtectedShadow {}
    public interface PackageShadow { default int stable() { return 81; } }
    public static class PackageBase {
        int shadow() { return 82; }
        public int oldCall() { return shadow(); }
    }
    public static class PackageImpl extends PackageBase implements PackageShadow {}
    // The package-private shadow() is declared in another runtime package.
    public interface OtherPackageShadow { default int stable() { return 91; } }
    public static class OtherPackageImpl extends com.github.ytlog.artdcevm.fixture.other.OtherPackageBase
            implements OtherPackageShadow {}
    // Loaded reflectively after evolution to exercise the normal ClassLinker path.
    public static class StaticLate extends StaticBase implements StaticShadow {}
    public static class PrivateLate extends PrivateImpl {}
    public interface ShadowParent { default int shadow() { return 7; } }
    public static class ProtectedPlain implements ProtectedShadow {}
    public static class ProtectedLate extends ProtectedBase implements ProtectedShadow {}
    public static class ProtectedWorkerFirst extends ProtectedBase implements ProtectedShadow {}
    public static class ProtectedWorkerSecond extends ProtectedBase implements ProtectedShadow {}
    public static class ProtectedWorkerNext extends ProtectedBase implements ProtectedShadow {}
    public static class PackageLate extends PackageBase implements PackageShadow {}
    public static class OtherPackageLate extends com.github.ytlog.artdcevm.fixture.other.OtherPackageBase
            implements OtherPackageShadow {}
    public static class ProtectedPublic extends ProtectedBase implements ProtectedShadow {
        public int shadow() { return 103; }
    }
    public interface BridgeParent<T> { T result(); }
    public interface BridgeContract extends BridgeParent<Integer> {
        default Integer result() { return 90; }

    }
    public static class BridgeImpl implements BridgeContract {
        public Integer result() { return BridgeContract.super.result(); }

    }
}
