package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Shared symbolic-resolution assertions for the pinned JBR and ART. */
public final class InterfaceSymbolState {
    public interface Check { void eq(String name, Object expected, Object actual) throws Exception; }
    public interface Call { Object run() throws Throwable; }
    public static Object result(Call call) {
        try { return call.run(); }
        catch (Throwable error) {
            while (error instanceof InvocationTargetException) error = error.getCause();
            return error.getClass().getName();
        }
    }
    public final InterfaceSymbolFixture.Receiver receiver = new InterfaceSymbolFixture.Receiver();
    public final InterfaceSymbolFixture.Lost lost = new InterfaceSymbolFixture.Lost();
    public final InterfaceSymbolFixture.Override override = new InterfaceSymbolFixture.Override();
    public final InterfaceSymbolFixture.Peer peer = new InterfaceSymbolFixture.Peer();
    private final int identity = System.identityHashCode(receiver);
    private final Class<?> child = InterfaceSymbolFixture.Child.class, parent = InterfaceSymbolFixture.Parent.class;
    public final Method cachedChild = child.getMethod("value"), cachedParent = parent.getMethod("value");
    private final MethodHandle childHandle = MethodHandles.publicLookup().findVirtual(child, "value", MethodType.methodType(int.class));
    private final MethodHandle parentHandle = MethodHandles.publicLookup().unreflect(cachedParent);
    private final MethodHandle classHandle = MethodHandles.publicLookup().findVirtual(InterfaceSymbolFixture.Lost.class, "value", MethodType.methodType(int.class));
    private final MethodHandle parentEcho = MethodHandles.publicLookup().findVirtual(parent,"echo",MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class));
    private final MethodHandle bodyErrorHandle = MethodHandles.publicLookup().findVirtual(parent, "badArgument", MethodType.methodType(int.class));
    public static class OrdinaryLookup {
        public InterfaceSymbolFixture.Result objectValue() { return null; }
        public int echo(InterfaceSymbolFixture.Result value) { return 7; }
    }
    public Class<?> aliasReturn;
    private Method lateChild, lateParent;
    public InterfaceSymbolState() throws Exception { }
    public Object freshChildHandleResult() {
        return result(() -> MethodHandles.publicLookup().findVirtual(child,"value",MethodType.methodType(int.class)).invokeWithArguments(receiver));
    }
    public void verifyFreshLookup(Check check, String prefix, String kind, boolean changed) throws Exception {
        MethodType type = MethodType.methodType(int.class);
        check.eq(prefix+"freshChildHandle", changed ? kind.equals("drop") ? "java.lang.NoSuchMethodException" :
                "java.lang.IllegalAccessException" : 7,
                freshChildHandleResult());
        check.eq(prefix+"wrongReturnHandle", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findVirtual(child,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"cachedParentEchoHandle", 7, result(() -> parentEcho.invokeWithArguments(receiver,new InterfaceSymbolFixture.Result())));
        check.eq(prefix+"freshParentEchoHandle", 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"echo",MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(receiver,new InterfaceSymbolFixture.Result())));
        check.eq(prefix+"freshObjectParentHandle", true,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"objectValue",MethodType.methodType(InterfaceSymbolFixture.Result.class)).type().returnType() == InterfaceSymbolFixture.Result.class));
        check.eq(prefix+"freshObjectChildHandle", changed && kind.equals("static") ? "java.lang.IllegalAccessException" :
                changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : true,
                result(() -> MethodHandles.publicLookup().findVirtual(child,"objectValue",MethodType.methodType(InterfaceSymbolFixture.Result.class)).type().returnType() == InterfaceSymbolFixture.Result.class));
        if (changed && kind.equals("static")) {
            check.eq(prefix+"aliasReturnDistinct", true, aliasReturn != InterfaceSymbolFixture.Result.class);
            check.eq(prefix+"aliasReturnSameName", true, aliasReturn.getName().equals(InterfaceSymbolFixture.Result.class.getName()));
            check.eq(prefix+"freshAliasedParameterHandle", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(child,"echo",MethodType.methodType(int.class,aliasReturn))));
            check.eq(prefix+"freshAliasedChildHandle", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(child,"objectValue",MethodType.methodType(aliasReturn))));
        }
        check.eq(prefix+"freshParentHandle", 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(receiver)));
        check.eq(prefix+"freshParentOverrideHandle", 17,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(override)));
        check.eq(prefix+"freshParentLostHandle", changed && kind.equals("drop") ? "java.lang.ClassCastException" : 7,
                result(() -> MethodHandles.publicLookup().findVirtual(parent,"value",type).invokeWithArguments(lost)));
        if (changed && kind.equals("static"))
            check.eq(prefix+"freshChildStaticHandle", 11,
                    result(() -> MethodHandles.publicLookup().findStatic(child,"value",type).invokeWithArguments()));
        check.eq(prefix+"freshParentStaticHandle", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"value",type).invokeWithArguments()));
        check.eq(prefix+"declaredChildReflection", changed && kind.equals("static") ? 11 :
                changed && kind.equals("private") ? "java.lang.IllegalAccessException" : "java.lang.NoSuchMethodException",
                result(() -> child.getDeclaredMethod("value").invoke(receiver)));
        check.eq(prefix+"declaredChildStatic", changed && !kind.equals("drop") ? kind.equals("static") : "java.lang.NoSuchMethodException",
                result(() -> java.lang.reflect.Modifier.isStatic(child.getDeclaredMethod("value").getModifiers())));
        check.eq(prefix+"unreflectChildStatic", changed && kind.equals("static") ? 11 :
                changed && kind.equals("private") ? "java.lang.IllegalAccessException" : "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().unreflect(child.getDeclaredMethod("value")).invokeWithArguments()));
        for (Class<?> owner : new Class<?>[]{parent, OrdinaryLookup.class}) {
            String key=prefix+(owner==parent?"interface":"ordinary");
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(aliasReturn))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"echo",MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(String.class))));
        }
        check.eq(prefix+"ordinaryStaticHandle", 9,
                result(() -> MethodHandles.publicLookup().findStatic(Math.class,"abs",MethodType.methodType(int.class,int.class)).invokeWithArguments(-9)));
    }
    public void verify(Check check, String prefix, String kind, boolean changed) throws Exception {
        Object failure = !changed ? null : kind.equals("drop") ? "java.lang.NoSuchMethodError"
                : kind.equals("private") ? "java.lang.IllegalAccessError" : "java.lang.IncompatibleClassChangeError";
        check.eq(prefix+"oldChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(receiver)));
        check.eq(prefix+"oldOverrideChild", changed ? failure : 17, result(() -> InterfaceSymbolFixture.Calls.child(override)));
        check.eq(prefix+"nullChild", changed ? failure : "java.lang.NullPointerException", result(() -> InterfaceSymbolFixture.Calls.child(null)));
        boolean removed = changed && kind.equals("drop");
        check.eq(prefix+"lostChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(lost)));
        check.eq(prefix+"lostParent", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> InterfaceSymbolFixture.Calls.lostParent(lost)));
        check.eq(prefix+"lostVirtual", removed ? "java.lang.NoSuchMethodError" : 7,
                result(() -> InterfaceSymbolFixture.Calls.lostVirtual(lost)));
        check.eq(prefix+"lostReflection", removed ? "java.lang.IllegalArgumentException" : 7,
                result(() -> cachedParent.invoke(lost)));
        check.eq(prefix+"lostChildHandle", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> childHandle.invokeWithArguments(lost)));
        check.eq(prefix+"lostParentHandle", removed ? "java.lang.ClassCastException" : 7,
                result(() -> parentHandle.invokeWithArguments(lost)));
        check.eq(prefix+"lostParentInstance", !removed, parent.isInstance(lost));
        check.eq(prefix+"lostChildExactHandle", removed ? "java.lang.IncompatibleClassChangeError" : 7,
                result(() -> (int) childHandle.invokeExact((InterfaceSymbolFixture.Child) lost)));
        check.eq(prefix+"lostClassHandle", removed ? "java.lang.AbstractMethodError" : 7,
                result(() -> classHandle.invokeWithArguments(lost)));
        check.eq(prefix+"nullChildHandle", "java.lang.NullPointerException", result(() -> childHandle.invokeWithArguments((Object) null)));
        check.eq(prefix+"bodyIllegalArgument", "java.lang.IllegalArgumentException", result(() -> bodyErrorHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"parentCall", 7, result(() -> InterfaceSymbolFixture.Calls.parent(receiver)));
        check.eq(prefix+"parentOverride", 17, result(() -> InterfaceSymbolFixture.Calls.parent(override)));
        check.eq(prefix+"virtualCall", 7, result(() -> InterfaceSymbolFixture.Calls.virtual(receiver)));
        check.eq(prefix+"virtualOverride", 17, result(() -> InterfaceSymbolFixture.Calls.virtual(override)));
        check.eq(prefix+"cachedChildOwner", true, cachedChild.getDeclaringClass() == parent);
        check.eq(prefix+"cachedChildReflection", 7, result(() -> cachedChild.invoke(receiver)));
        check.eq(prefix+"cachedParentReflection", 7, result(() -> cachedParent.invoke(receiver)));
        check.eq(prefix+"cachedChildHandle", 7, result(() -> childHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"cachedParentHandle", 7, result(() -> parentHandle.invokeWithArguments(receiver)));
        check.eq(prefix+"freshChildLookup", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" :
                changed && kind.equals("static") ? 11 : 7, result(() -> child.getMethod("value").invoke(receiver)));
        check.eq(prefix+"freshParentReflection", 7, result(() -> parent.getMethod("value").invoke(receiver)));
        check.eq(prefix+"childInstance", true, child.isInstance(receiver));
        check.eq(prefix+"parentInstance", true, parent.isInstance(receiver));
        check.eq(prefix+"identity", true, identity == System.identityHashCode(receiver));
        check.eq(prefix+"marker", 31, receiver.marker);
        check.eq(prefix+"peerCall", changed ? 5 : 3, peer.value());
        if (lateChild == null && changed) {
            Class<?> late = Class.forName("com.github.ytlog.artdcevm.fixture.InterfaceSymbolFixture$LateCalls");
            lateChild = late.getMethod("child", child); lateParent = late.getMethod("parent", parent);
        }
        if (lateChild != null) {
            check.eq(prefix+"lateChild", changed ? failure : 7, result(() -> lateChild.invoke(null,receiver)));
            check.eq(prefix+"lateParent", 7, result(() -> lateParent.invoke(null,receiver)));
        }
        check.eq(prefix+"newReceiverChild", changed ? failure : 7, result(() -> InterfaceSymbolFixture.Calls.child(new InterfaceSymbolFixture.Receiver())));
    }
}
