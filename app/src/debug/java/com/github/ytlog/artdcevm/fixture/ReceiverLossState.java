package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.util.Collections;

/** Cached Java members are created before either receiver loses its interface. */
public final class ReceiverLossState {
    public interface Check { void eq(String name, Object expected, Object actual) throws Exception; }
    public final ReceiverLossFixture.DefaultOnly defaultReceiver = new ReceiverLossFixture.DefaultOnly();
    public final ReceiverLossFixture.Own ownReceiver = new ReceiverLossFixture.Own();
    public final ReceiverLossFixture.Peer peer = new ReceiverLossFixture.Peer();
    private static ReceiverLossFixture.Contract typedDefault, typedOwn;
    private final Object[] receivers = {defaultReceiver, ownReceiver};
    private final int[] identities = {System.identityHashCode(defaultReceiver), System.identityHashCode(ownReceiver)};
    private final MethodHandle interfaceHandle = MethodHandles.publicLookup().findVirtual(
            ReceiverLossFixture.Contract.class, "value", MethodType.methodType(int.class));
    private final MethodHandle adapter = interfaceHandle.asType(MethodType.methodType(int.class, Object.class));
    private final Method interfaceMethod = ReceiverLossFixture.Contract.class.getMethod("value");
    private final MethodHandle[] classHandles = new MethodHandle[2], boundInterface = new MethodHandle[2], boundClass = new MethodHandle[2], boundBody = new MethodHandle[2];
    private final MethodHandle[] boundArguments = new MethodHandle[2], caught = new MethodHandle[2], spread = new MethodHandle[2], boxed = new MethodHandle[2], dropped = new MethodHandle[2];
    private final Method[] classMethods = new Method[2];
    private final MethodHandle nullBound = interfaceHandle.bindTo(null);
    private final ReceiverLossAdapters[] adapters = new ReceiverLossAdapters[2];
    private Object[] late;

    public ReceiverLossState() throws Exception {
        typedDefault = defaultReceiver; typedOwn = ownReceiver;
        for (int i = 0; i < 2; ++i) {
            classHandles[i] = MethodHandles.publicLookup().findVirtual(receivers[i].getClass(), "value", MethodType.methodType(int.class));
            boundInterface[i] = interfaceHandle.bindTo(i == 0 ? typedDefault : typedOwn);
            boundClass[i] = classHandles[i].bindTo(receivers[i]);
            boundBody[i] = MethodHandles.publicLookup().findVirtual(ReceiverLossFixture.Contract.class, "bodyError", MethodType.methodType(int.class)).bindTo(receivers[i]);
            boundArguments[i] = MethodHandles.publicLookup().findVirtual(ReceiverLossFixture.Contract.class,
                    "arguments", MethodType.methodType(int.class, int.class, String.class)).bindTo(receivers[i]);
            caught[i] = MethodHandles.catchException(boundInterface[i], IncompatibleClassChangeError.class,
                    MethodHandles.publicLookup().findStatic(ReceiverLossState.class, "recovered",
                            MethodType.methodType(int.class, IncompatibleClassChangeError.class)));
            spread[i] = boundInterface[i].asSpreader(Object[].class, 0);
            boxed[i] = boundInterface[i].asType(MethodType.methodType(Object.class));
            dropped[i] = MethodHandles.dropArguments(boundInterface[i], 0, String.class);
            classMethods[i] = receivers[i].getClass().getMethod("value");
            adapters[i] = new ReceiverLossAdapters(i == 0 ? typedDefault : typedOwn);
        }
    }
    public static int recovered(IncompatibleClassChangeError ignored) { return 31; }
    private int classExact(int kind) throws Throwable {
        return kind == 0 ? (int) classHandles[0].invokeExact(defaultReceiver) : (int) classHandles[1].invokeExact(ownReceiver);
    }
    public void loadLate() throws Exception {
        if (late != null) return;
        String root = "com.github.ytlog.artdcevm.fixture.ReceiverLossLateFixture$";
        late = new Object[]{Class.forName(root + "DefaultLate").getConstructor().newInstance(),
                Class.forName(root + "OwnLate").getConstructor().newInstance()};
    }
    public void verify(Check check, String prefix, boolean changed) throws Exception {
        for (int i = 0; i < 2; ++i) {
            final int kind = i;
            Object receiver = receivers[i];
            ReceiverLossFixture.Contract saved = i == 0 ? typedDefault : typedOwn;
            int value = i == 0 ? 7 : 11;
            String p = prefix + i + ".";
            Object interfaceResult = changed ? "java.lang.IncompatibleClassChangeError" : value;
            Object castResult = changed ? "java.lang.ClassCastException" : value;
            Object classResult = changed && i == 0 ? "java.lang.AbstractMethodError" : value;
            for (java.util.Map.Entry<String, String> entry : adapters[i].observations().entrySet()) {
                check.eq(p + "adapter" + entry.getKey(), ReceiverLossAdapters.expected(entry.getKey(), changed, value), entry.getValue());
            }
            check.eq(p + "interface", !changed, ReceiverLossFixture.Contract.class.isInstance(receiver));
            check.eq(p + "direct", interfaceResult, InterfaceSymbolState.result(() -> saved.value()));
            check.eq(p + "interfaceExact", interfaceResult, InterfaceSymbolState.result(() -> (int) interfaceHandle.invokeExact(saved)));
            check.eq(p + "interfaceArgs", castResult, InterfaceSymbolState.result(() -> interfaceHandle.invokeWithArguments(receiver)));
            check.eq(p + "interfaceBound", interfaceResult, InterfaceSymbolState.result(() -> (int) boundInterface[kind].invokeExact()));
            check.eq(p + "interfaceAdapter", castResult, InterfaceSymbolState.result(() -> (int) adapter.invokeExact(receiver)));
            check.eq(p + "interfaceBoundArgs", interfaceResult, InterfaceSymbolState.result(() -> boundInterface[kind].invokeWithArguments()));
            check.eq(p + "boundArgsList", interfaceResult, InterfaceSymbolState.result(() -> boundInterface[kind].invokeWithArguments(Collections.emptyList())));
            check.eq(p + "boundArgsNullArray", interfaceResult, InterfaceSymbolState.result(() -> boundInterface[kind].invokeWithArguments((Object[]) null)));
            check.eq(p + "boundArgsWrongArity", "java.lang.invoke.WrongMethodTypeException", InterfaceSymbolState.result(() -> boundInterface[kind].invokeWithArguments(1)));
            check.eq(p + "boundArgsNullList", "java.lang.NullPointerException", InterfaceSymbolState.result(() -> boundInterface[kind].invokeWithArguments((java.util.List<?>) null)));
            check.eq(p + "boundBoxed", interfaceResult, InterfaceSymbolState.result(() -> (Object) boxed[kind].invokeExact()));
            check.eq(p + "boundSpread", interfaceResult, InterfaceSymbolState.result(() -> (int) spread[kind].invokeExact(new Object[0])));
            check.eq(p + "boundSpreadWrongLength", "java.lang.IllegalArgumentException", InterfaceSymbolState.result(() -> (int) spread[kind].invokeExact(new Object[]{1})));
            check.eq(p + "boundCaught", changed ? 31 : value, InterfaceSymbolState.result(() -> caught[kind].invokeWithArguments()));
            check.eq(p + "boundDropped", interfaceResult, InterfaceSymbolState.result(() -> dropped[kind].invokeWithArguments("ignored")));
            check.eq(p + "boundDroppedWrongReference", "java.lang.ClassCastException", InterfaceSymbolState.result(() -> dropped[kind].invokeWithArguments(new Object())));
            check.eq(p + "boundNullArgs", "java.lang.NullPointerException", InterfaceSymbolState.result(() -> nullBound.invokeWithArguments()));
            check.eq(p + "boundBodyArgs", changed ? "java.lang.IncompatibleClassChangeError" : "java.lang.IllegalStateException", InterfaceSymbolState.result(() -> boundBody[kind].invokeWithArguments()));
            Object argumentResult = changed ? "java.lang.IncompatibleClassChangeError" : 8;
            check.eq(p + "boundParameters", argumentResult, InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(5, "abc")));
            check.eq(p + "boundWidening", argumentResult, InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments((short) 5, "abc")));
            check.eq(p + "boundBadReference", "java.lang.ClassCastException", InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(5, new Object())));
            check.eq(p + "boundBadPrimitive", "java.lang.ClassCastException", InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(5.5, "abc")));
            check.eq(p + "boundNullPrimitive", "java.lang.NullPointerException", InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(null, "abc")));
            check.eq(p + "boundNullReference", changed ? "java.lang.IncompatibleClassChangeError" : "java.lang.NullPointerException", InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(5, null)));
            check.eq(p + "boundParameterArity", "java.lang.invoke.WrongMethodTypeException", InterfaceSymbolState.result(() -> boundArguments[kind].invokeWithArguments(5)));
            check.eq(p + "boundWrongReturn", "java.lang.invoke.WrongMethodTypeException", InterfaceSymbolState.result(() -> (long) boundInterface[kind].invokeExact()));
            check.eq(p + "boundWrongArgument", "java.lang.invoke.WrongMethodTypeException", InterfaceSymbolState.result(() -> (int) boundInterface[kind].invokeExact(1)));
            check.eq(p + "boundNull", "java.lang.NullPointerException", InterfaceSymbolState.result(() -> (int) nullBound.invokeExact()));
            check.eq(p + "boundBody", changed ? "java.lang.IncompatibleClassChangeError" : "java.lang.IllegalStateException", InterfaceSymbolState.result(() -> (int) boundBody[kind].invokeExact()));
            check.eq(p + "classExact", classResult, InterfaceSymbolState.result(() -> classExact(kind)));
            check.eq(p + "classArgs", classResult, InterfaceSymbolState.result(() -> classHandles[kind].invokeWithArguments(receiver)));
            check.eq(p + "classBound", classResult, InterfaceSymbolState.result(() -> (int) boundClass[kind].invokeExact()));
            check.eq(p + "classBoundArgs", classResult, InterfaceSymbolState.result(() -> boundClass[kind].invokeWithArguments()));
            check.eq(p + "interfaceReflection", changed ? "java.lang.IllegalArgumentException" : value,
                    InterfaceSymbolState.result(() -> interfaceMethod.invoke(receiver)));
            check.eq(p + "classReflection", changed && i == 0 ? "java.lang.IllegalArgumentException" : value,
                    InterfaceSymbolState.result(() -> classMethods[kind].invoke(receiver)));
            check.eq(p + "freshClassHandle", changed && i == 0 ? "java.lang.NoSuchMethodException" : value,
                    InterfaceSymbolState.result(() -> MethodHandles.publicLookup().findVirtual(receiver.getClass(), "value", MethodType.methodType(int.class)).invokeWithArguments(receiver)));
            check.eq(p + "identity", true, identities[i] == System.identityHashCode(receiver));
            check.eq(p + "self", true, receiver.getClass().getField("self").get(receiver) == receiver);
            check.eq(p + "kept", i == 0 ? 19 : 23, receiver.getClass().getField("kept").getInt(receiver));
            if (late != null) {
                check.eq(p + "lateInterface", !changed, ReceiverLossFixture.Contract.class.isInstance(late[i]));
                check.eq(p + "lateClassHandle", classResult, InterfaceSymbolState.result(() -> classHandles[kind].invokeWithArguments(late[kind])));
            }
        }
        check.eq(prefix + "peer", changed ? 13 : 3, peer.value());
    }
}
