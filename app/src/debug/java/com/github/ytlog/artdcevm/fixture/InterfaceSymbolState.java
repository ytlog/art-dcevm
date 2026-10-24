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
        public static InterfaceSymbolFixture.Result staticObject(InterfaceSymbolFixture.Result value) { return value; }
        public static int staticEcho(InterfaceSymbolFixture.Result value) { return value == null ? 41 : 29; }
        private static int privateStatic() { return 67; }
        protected static int protectedStatic() { return 71; }
        public static int throwingStatic() { throw new IllegalStateException(); }
    }
    static class HiddenStaticBase { public static int publicStatic() { return 73; } }
    public static class PublicStaticChild extends HiddenStaticBase { }
    public interface StaticInterface { static int interfaceOnly() { return 79; } }
    public static class StaticImplementor implements StaticInterface { }
    static class PackageStaticOwner {
        public static InterfaceSymbolFixture.Result object(InterfaceSymbolFixture.Result value) { return value; }
        public static int echo(InterfaceSymbolFixture.Result value) { return value == null ? 83 : 89; }
    }
    static class PackageStaticMiddle extends PackageStaticOwner { }
    static class PackageStaticLeaf extends PackageStaticMiddle { }
    static class PackagePeerChild extends InterfaceSymbolFixture.Peer { }
    private MethodHandle packagePeerHandle;
    public static class PeerStaticChild extends InterfaceSymbolFixture.Peer { }
    private MethodHandle inheritedPeerHandle;
    public static class StaticMiddle extends OrdinaryLookup { }
    public static class StaticLeaf extends StaticMiddle { }
    public static class StaticHiding extends StaticMiddle {
        public static int staticEcho(InterfaceSymbolFixture.Result value) { return value == null ? 47 : 43; }
        public static int staticEcho(String value) { return 53; }
    }
    // A distinct multi-level hierarchy keeps recursive lookup independent of
    // the Child declarations being changed by the transaction matrix.
    public interface LookupMiddle extends InterfaceSymbolFixture.Parent {}
    public interface LookupLeaf extends LookupMiddle {}
    public static class LookupReceiver implements LookupLeaf {}
    public static class CovariantLeaf extends InterfaceSymbolFixture.Peer { }
    public static class CovariantOverride extends InterfaceSymbolFixture.Peer {
        @Override public String covariant(boolean fail) { return "override"; }
    }
    public interface CovariantRoot { Object covariant(boolean fail); default int rank() { return 1; } }
    public interface CovariantInterface extends CovariantRoot {
        @Override default int rank() { return 7; }
        @Override default String covariant(boolean fail) { return new InterfaceSymbolFixture.Peer().covariant(fail); }
    }
    public interface CovariantInterfaceLeaf extends CovariantInterface { }
    public static class CovariantReceiver implements CovariantInterfaceLeaf { }
    public interface EnumerationLeft extends CovariantInterface { }
    public interface EnumerationRight extends CovariantInterface { }
    public interface EnumerationExtra { default int extra() { return 97; } }
    public interface EnumerationDiamond extends EnumerationLeft, EnumerationRight, EnumerationExtra { }
    public interface EnumerationReverse extends EnumerationRight, EnumerationLeft, EnumerationExtra { }
    public static class EnumerationReceiver implements EnumerationDiamond { }
    private final Method[] enumerationCache = new Method[9];
    private static Method enumerated(Class<?> owner, String name, Class<?> result, Class<?>... parameters) throws Exception {
        Method selected = null;
        for (Method method : owner.getMethods()) {
            if (!method.getName().equals(name) || method.getReturnType() != result
                    || !java.util.Arrays.equals(method.getParameterTypes(),parameters)) continue;
            if (selected != null) throw new AssertionError("duplicate enumeration for " + name);
            selected = method;
        }
        if (selected == null) throw new NoSuchMethodException(name);
        return selected;
    }
    private void verifyEnumeration(Check check, String prefix, boolean changed) throws Exception {
        Class<?>[] owners = {InterfaceSymbolFixture.Peer.class, CovariantLeaf.class, CovariantInterface.class,
                CovariantInterfaceLeaf.class, CovariantReceiver.class, EnumerationDiamond.class,
                EnumerationReverse.class, EnumerationReceiver.class, CovariantOverride.class};
        Object[] targets = {peer, new CovariantLeaf(), new CovariantReceiver(), new CovariantReceiver(),
                new CovariantReceiver(), new EnumerationReceiver(), new EnumerationReceiver(),
                new EnumerationReceiver(), new CovariantOverride()};
        for (int i = 0; i < owners.length; ++i) {
            final int slot = i; Class<?> owner = owners[i]; Object target = targets[i];
            Class<?> declaring = i < 2 ? InterfaceSymbolFixture.Peer.class : i == 8 ? CovariantOverride.class : CovariantInterface.class;
            String key = prefix + "enumeration" + i;
            String value = i == 8 ? "override" : changed ? "after" : "before";
            Method bridge = enumerated(owner,"covariant",Object.class,boolean.class);
            Method narrow = enumerated(owner,"covariant",String.class,boolean.class);
            check.eq(key+"BridgeOwner", true, bridge.getDeclaringClass() == declaring);
            check.eq(key+"Bridge", true, bridge.isBridge());
            check.eq(key+"Synthetic", true, bridge.isSynthetic());
            check.eq(key+"Default", i >= 2 && i < 8, bridge.isDefault());
            check.eq(key+"NarrowOwner", true, narrow.getDeclaringClass() == declaring);
            check.eq(key+"Call", value, result(() -> bridge.invoke(target,false)));
            check.eq(key+"NarrowCall", value, result(() -> narrow.invoke(target,false)));
            check.eq(key+"Cached", value, result(() -> {
                if (enumerationCache[slot] == null) enumerationCache[slot] = bridge;
                return enumerationCache[slot].invoke(target,false);
            }));
            check.eq(key+"Null", "java.lang.NullPointerException", result(() -> bridge.invoke(null,false)));
            check.eq(key+"Body", i == 8 ? "override" : "java.lang.IllegalStateException", result(() -> bridge.invoke(target,true)));
            check.eq(key+"Declared", i == 0 || i == 2 || i == 8,
                    java.util.Arrays.stream(owner.getDeclaredMethods()).anyMatch(m -> m.getName().equals("covariant")));
            if (i >= 2 && i < 8) {
                Method rank = enumerated(owner,"rank",int.class);
                check.eq(key+"RankOwner", true, rank.getDeclaringClass() == CovariantInterface.class);
                check.eq(key+"RankCall", 7, rank.invoke(target));
            }
        }
        Method extra = enumerated(EnumerationDiamond.class,"extra",int.class);
        check.eq(prefix+"enumerationExtraOwner", true, extra.getDeclaringClass() == EnumerationExtra.class);
        check.eq(prefix+"enumerationExtraCall", 97, extra.invoke(new EnumerationReceiver()));
    }
    private final MethodHandle[] covariantHandles = new MethodHandle[5];
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
        for (Class<?> owner : new Class<?>[]{parent, OrdinaryLookup.class, LookupMiddle.class, LookupLeaf.class, LookupReceiver.class}) {
            String key=prefix+(owner==parent?"interface":owner==OrdinaryLookup.class?"ordinary":owner==LookupMiddle.class?"middle":owner==LookupLeaf.class?"leaf":"inheritedClass");
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(aliasReturn))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"echo",MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findVirtual(owner,"objectValue",MethodType.methodType(String.class))));
            if (owner != parent && owner != OrdinaryLookup.class) {
                check.eq(key+"MatchingCall", 7, result(() -> MethodHandles.publicLookup().findVirtual(owner,"echo",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))
                        .invokeWithArguments(new LookupReceiver(),new InterfaceSymbolFixture.Result())));
                check.eq(key+"ReflectionAlias", "java.lang.NoSuchMethodException",
                        result(() -> owner.getMethod("echo",aliasReturn)));
            }
        }
        check.eq(prefix+"childStaticResolution", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : changed && kind.equals("static") ? 11 : "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"value",type).invokeWithArguments()));
        check.eq(prefix+"childStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"parentStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"value",MethodType.methodType(String.class))));
        check.eq(prefix+"parentStaticAliasedParameter", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(parent,"echo",MethodType.methodType(int.class,aliasReturn))));
        check.eq(prefix+"childStaticAliasedReturn", changed && kind.equals("drop") ? "java.lang.NoSuchMethodException" : "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"objectValue",MethodType.methodType(aliasReturn))));
        check.eq(prefix+"childStaticWrongParameter", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(child,"echo",MethodType.methodType(int.class,String.class))));
        check.eq(prefix+"childStaticNullName", "java.lang.NullPointerException",
                result(() -> MethodHandles.publicLookup().findStatic(child,null,type)));
        check.eq(prefix+"ordinaryStaticHandle", 9,
                result(() -> MethodHandles.publicLookup().findStatic(Math.class,"abs",MethodType.methodType(int.class,int.class)).invokeWithArguments(-9)));
        check.eq(prefix+"ordinaryStaticAliasedReturn", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
        check.eq(prefix+"ordinaryStaticAliasedParameter", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,aliasReturn))));
        check.eq(prefix+"ordinaryStaticAliasedBoth", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(aliasReturn,aliasReturn))));
        check.eq(prefix+"ordinaryStaticWrongReturn", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
        check.eq(prefix+"ordinaryStaticWrongParameter", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,String.class))));
        InterfaceSymbolFixture.Result argument = new InterfaceSymbolFixture.Result();
        check.eq(prefix+"ordinaryStaticMatchingCall", 29,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
        check.eq(prefix+"ordinaryStaticNullCall", 41,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticEcho",
                        MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
        check.eq(prefix+"ordinaryStaticIdentityCall", true,
                result(() -> MethodHandles.publicLookup().findStatic(OrdinaryLookup.class,"staticObject",
                        MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                        .invokeWithArguments(argument) == argument));
        for (Class<?> owner : new Class<?>[]{StaticMiddle.class, StaticLeaf.class, StaticHiding.class}) {
            String key = prefix + (owner == StaticMiddle.class ? "staticMiddle" : owner == StaticLeaf.class ? "staticLeaf" : "staticHiding");
            check.eq(key+"MatchingCall", owner == StaticHiding.class ? 43 : 29,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
            check.eq(key+"NullCall", owner == StaticHiding.class ? 47 : 41,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
            check.eq(key+"IdentityCall", true,
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticObject",
                            MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                            .invokeWithArguments(argument) == argument));
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticObject",
                            MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"Overload", owner == StaticHiding.class ? 53 : "java.lang.NoSuchMethodException",
                    result(() -> MethodHandles.publicLookup().findStatic(owner,"staticEcho",
                            MethodType.methodType(int.class,String.class)).invokeWithArguments("argument")));
            check.eq(key+"DeclaredReflection", "java.lang.NoSuchMethodException",
                    result(() -> owner.getDeclaredMethod("staticObject",InterfaceSymbolFixture.Result.class)));
        }
        check.eq(prefix+"inheritedPrivateAccess", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"privateStatic",type)));
        check.eq(prefix+"inheritedProtectedAccess", "java.lang.IllegalAccessException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"protectedStatic",type)));
        check.eq(prefix+"inheritedBodyException", "java.lang.IllegalStateException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticLeaf.class,"throwingStatic",type).invokeWithArguments()));
        check.eq(prefix+"hiddenDeclaringClassCall", 73,
                result(() -> MethodHandles.publicLookup().findStatic(PublicStaticChild.class,"publicStatic",type).invokeWithArguments()));
        check.eq(prefix+"interfaceStaticNotInherited", "java.lang.NoSuchMethodException",
                result(() -> MethodHandles.publicLookup().findStatic(StaticImplementor.class,"interfaceOnly",type)));
        check.eq(prefix+"inheritedPeerFresh", changed ? 61 : 59,
                result(() -> MethodHandles.publicLookup().findStatic(PeerStaticChild.class,"staticValue",type).invokeWithArguments()));
        check.eq(prefix+"inheritedPeerCached", changed ? 61 : 59, result(() -> {
            if (inheritedPeerHandle == null) inheritedPeerHandle = MethodHandles.publicLookup().findStatic(PeerStaticChild.class,"staticValue",type);
            return (int) inheritedPeerHandle.invokeExact();
        }));
        MethodHandles.Lookup foreignAccess = (MethodHandles.Lookup) aliasReturn.getMethod("lookup").invoke(null);
        check.eq(prefix+"packageForeignLookupClass", true, foreignAccess.lookupClass() == aliasReturn);
        for (Class<?> owner : new Class<?>[]{PackageStaticOwner.class, PackageStaticLeaf.class}) {
            String key = prefix + (owner == PackageStaticOwner.class ? "packageDeclared" : "packageInherited");
            MethodHandles.Lookup access = MethodHandles.lookup();
            check.eq(key+"Call", 89, result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments(argument)));
            check.eq(key+"NullCall", 83, result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class)).invokeWithArguments((Object)null)));
            check.eq(key+"IdentityCall", true, result(() -> access.findStatic(owner,"object",
                    MethodType.methodType(InterfaceSymbolFixture.Result.class,InterfaceSymbolFixture.Result.class))
                    .invokeWithArguments(argument) == argument));
            check.eq(key+"AliasedReturn", "java.lang.IllegalAccessException", result(() -> access.findStatic(owner,"object",
                    MethodType.methodType(aliasReturn,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"AliasedParameter", "java.lang.IllegalAccessException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,aliasReturn))));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(String.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"PublicDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup().findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"NoAccessDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup().in(owner).findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"ForeignPackageDenied", "java.lang.IllegalAccessException", result(() -> foreignAccess.findStatic(owner,"echo",
                    MethodType.methodType(int.class,InterfaceSymbolFixture.Result.class))));
            check.eq(key+"WrongParameter", "java.lang.NoSuchMethodException", result(() -> access.findStatic(owner,"echo",
                    MethodType.methodType(int.class,String.class))));
            check.eq(key+"ReflectionAlias", "java.lang.NoSuchMethodException", result(() -> owner.getDeclaredMethod("echo",aliasReturn)));
        }
        check.eq(prefix+"packagePeerFresh", changed ? 61 : 59, result(() -> MethodHandles.lookup()
                .findStatic(PackagePeerChild.class,"staticValue",type).invokeWithArguments()));
        check.eq(prefix+"packagePeerCached", changed ? 61 : 59, result(() -> {
            if (packagePeerHandle == null) packagePeerHandle = MethodHandles.lookup().findStatic(PackagePeerChild.class,"staticValue",type);
            return (int) packagePeerHandle.invokeExact();
        }));
        Class<?>[] bridgeOwners = {InterfaceSymbolFixture.Peer.class, CovariantLeaf.class,
                CovariantInterface.class, CovariantInterfaceLeaf.class, CovariantReceiver.class};
        Object[] bridgeReceivers = {peer, new CovariantLeaf(), new CovariantReceiver(),
                new CovariantReceiver(), new CovariantReceiver()};
        for (int i = 0; i < bridgeOwners.length; ++i) {
            final int slot = i;
            Class<?> owner = bridgeOwners[i]; Object target = bridgeReceivers[i];
            String key = prefix + "covariant" + i;
            MethodType bridgeType = MethodType.methodType(Object.class, boolean.class);
            check.eq(key+"Call", changed ? "after" : "before", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(target,false)));
            check.eq(key+"Cached", changed ? "after" : "before", result(() -> {
                if (covariantHandles[slot] == null) covariantHandles[slot] = MethodHandles.publicLookup()
                        .findVirtual(owner,"covariant",bridgeType);
                return covariantHandles[slot].invokeWithArguments(target,false);
            }));
            check.eq(key+"NarrowCall", changed ? "after" : "before", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",MethodType.methodType(String.class,boolean.class))
                    .invokeWithArguments(target,false)));
            check.eq(key+"Type", true, result(() -> MethodHandles.publicLookup().findVirtual(owner,"covariant",bridgeType)
                    .type().equals(bridgeType.insertParameterTypes(0,owner))));
            check.eq(key+"Null", "java.lang.NullPointerException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(null,false)));
            check.eq(key+"BodyException", "java.lang.IllegalStateException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",bridgeType).invokeWithArguments(target,true)));
            check.eq(key+"WrongReturn", "java.lang.NoSuchMethodException", result(() -> MethodHandles.publicLookup()
                    .findVirtual(owner,"covariant",MethodType.methodType(Number.class,boolean.class))));
            boolean hasBridge = false;
            for (Method method : (i < 2 ? InterfaceSymbolFixture.Peer.class : CovariantInterface.class).getDeclaredMethods())
                hasBridge |= method.getName().equals("covariant") && method.isBridge()
                        && method.getReturnType() == Object.class;
            check.eq(key+"BridgePresent", true, hasBridge);
            check.eq(key+"AccessDenied", "java.lang.IllegalAccessException", result(() -> MethodHandles.publicLookup()
                    .in(PackageStaticOwner.class).findVirtual(owner,"covariant",bridgeType)));
            check.eq(key+"Reflection", true, owner.getMethod("covariant",boolean.class).getReturnType() == String.class);
        }
        check.eq(prefix+"covariantOverride", "override", result(() -> MethodHandles.publicLookup()
                .findVirtual(InterfaceSymbolFixture.Peer.class,"covariant",MethodType.methodType(Object.class,boolean.class))
                .invokeWithArguments(new CovariantOverride(),false)));
        check.eq(prefix+"covariantExact", changed ? "after" : "before", result(() -> {
            MethodHandle handle = MethodHandles.publicLookup().findVirtual(InterfaceSymbolFixture.Peer.class,
                    "covariant",MethodType.methodType(Object.class,boolean.class));
            return (Object) handle.invokeExact(peer,false);
        }));
        verifyEnumeration(check,prefix,changed);
        check.eq(prefix+"ordinaryStaticReflectionAlias", "java.lang.NoSuchMethodException",
                result(() -> OrdinaryLookup.class.getDeclaredMethod("staticEcho",aliasReturn)));
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
