package com.github.ytlog.artdcevm.fixture.selection;
public final class NewCalls {
    public static int call(Matrix.Contract receiver) { return receiver.select(); }
    public static int stable(Matrix.Contract receiver) { return receiver.stable(); }
    public static int hot(Matrix.Contract receiver, int count) {
        int result = 0;
        for (int i = 0; i < count; ++i) {
            try { result += receiver.select(); }
            catch (IllegalAccessError expected) { result -= 1; }
            catch (AbstractMethodError expected) { result -= 2; }
        }
        return result;
    }
    public static int classCall(Object receiver, int row) {
        switch (row) {
            case 0: return ((Matrix.Plain) receiver).oldCall();
            case 1: return ((Matrix.OwnPrivate) receiver).oldCall();
            case 2: return ((Matrix.PrivateChild) receiver).oldCall();
            case 3: return ((Matrix.PrivateGrandchild) receiver).oldCall();
            case 4: return ((Matrix.OwnPackage) receiver).oldCall();
            case 5: return ((Matrix.PackageChild) receiver).oldCall();
            case 6: return ((Matrix.OwnProtected) receiver).oldCall();
            case 7: return ((Matrix.ProtectedChild) receiver).oldCall();
            case 8: return ((Matrix.OtherPackageChild) receiver).oldCall();
            case 9: return ((Matrix.PrivatePublic) receiver).oldCall();
            case 10: return ((Matrix.PackagePublic) receiver).oldCall();
            case 11: return ((Matrix.ProtectedPublic) receiver).oldCall();
            case 12: return ((Matrix.OtherPackagePublic) receiver).oldCall();
            case 13: return ((Matrix.OwnPrivateLate) receiver).oldCall();
            case 14: return ((Matrix.PrivateLate) receiver).oldCall();
            case 15: return ((Matrix.PackageLate) receiver).oldCall();
            case 16: return ((Matrix.ProtectedLate) receiver).oldCall();
            case 17: return ((Matrix.OtherPackageLate) receiver).oldCall();
            case 18: return ((Matrix.PublicLate) receiver).oldCall();
            case 19: return ((Matrix.RedeclaredPrivate) receiver).oldCall();
            case 20: return ((Matrix.OwnOnInherited) receiver).oldCall();
            case 21: return ((Matrix.OwnOnPlain) receiver).oldCall();
            default: throw new AssertionError(row);
        }
    }
}
