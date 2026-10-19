package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

/** Shared assertions for the pinned JBR oracle and ART instrumentation. */
public final class MemberDescriptorState {
    public interface Check { void eq(String name, Object expected, Object actual) throws Exception; }
    public final MemberDescriptorFixture.Implementation receiver = new MemberDescriptorFixture.Implementation();
    public final MemberDescriptorFixture.Peer peer = new MemberDescriptorFixture.Peer();
    private final Object token = MemberDescriptorFixture.Contract.TOKEN, peerToken = peer.token;
    private final Class<?> contract = MemberDescriptorFixture.Contract.class;
    private final Method value = contract.getDeclaredMethod("value"), sum = contract.getDeclaredMethod("sum", int[].class);
    private final Method inherited = receiver.getClass().getMethod("value");
    private final Field field = contract.getDeclaredField("TOKEN"), peerField = peer.getClass().getField("token");
    private final MethodHandle handle = MethodHandles.publicLookup().unreflect(value);
    private Method late;

    public MemberDescriptorState() throws Exception { }

    public void verify(Check check, String prefix, String kind, boolean changed) throws Exception {
        boolean all = kind.equals("all"), synthetic = all || kind.equals("synthetic");
        boolean bridge = all || kind.equals("bridge"), varargs = all || kind.equals("varargs");
        boolean strict = all || kind.equals("strict"), fields = all || kind.equals("field");
        String[] names = {"value", "sum", "half", "abstractSum", "hidden", "throughPrivate", "staticValue"};
        int[] flags = {1, 1, 1, 0x401, 2, 1, 9};
        if (changed) {
            if (synthetic) { flags[0] |= 0x1000; flags[4] |= 0x1000; flags[6] |= 0x1000; }
            if (bridge) flags[0] |= 0x40;
            if (varargs) { flags[1] |= 0x80; flags[3] |= all ? 0x10c0 : 0x80; }
            if (strict) flags[2] |= 0x800;
            if (all) flags[5] |= 0x1000;
        }
        for (int i = 0; i < names.length; ++i) {
            Class<?>[] parameters = i == 1 || i == 3 ? new Class<?>[]{int[].class}
                    : i == 2 ? new Class<?>[]{double.class} : new Class<?>[0];
            Method method = contract.getDeclaredMethod(names[i], parameters);
            check.eq(prefix + names[i] + "Flags", flags[i], method.getModifiers());
            check.eq(prefix + names[i] + "Synthetic", (flags[i] & 0x1000) != 0, method.isSynthetic());
            check.eq(prefix + names[i] + "Bridge", (flags[i] & 0x40) != 0, method.isBridge());
            check.eq(prefix + names[i] + "Varargs", (flags[i] & 0x80) != 0, method.isVarArgs());
            check.eq(prefix + names[i] + "Strict", (flags[i] & 0x800) != 0, Modifier.isStrict(method.getModifiers()));
        }
        check.eq(prefix + "fieldFlags", changed && fields ? 0x1019 : 0x19, contract.getField("TOKEN").getModifiers());
        check.eq(prefix + "peerFieldFlags", changed && fields ? 0x1001 : 1, peer.getClass().getField("token").getModifiers());
        check.eq(prefix + "inheritedFlags", flags[0], receiver.getClass().getMethod("value").getModifiers());
        check.eq(prefix + "cachedInheritedFlags", 1, inherited.getModifiers());
        check.eq(prefix + "cachedInheritedReflection", changed ? 11 : 7, inherited.invoke(receiver));
        check.eq(prefix + "virtualCall", changed ? 11 : 7, receiver.value());
        check.eq(prefix + "cachedFlags", 1, value.getModifiers());
        check.eq(prefix + "cachedFieldFlags", 0x19, field.getModifiers());
        check.eq(prefix + "cachedPeerFieldFlags", 1, peerField.getModifiers());
        check.eq(prefix + "oldCall", changed ? 64 : 52, MemberDescriptorFixture.Calls.call(receiver));
        check.eq(prefix + "newInstance", changed ? 64 : 52,
                MemberDescriptorFixture.Calls.call(new MemberDescriptorFixture.Implementation()));
        check.eq(prefix + "cachedReflection", changed ? 11 : 7, value.invoke(receiver));
        check.eq(prefix + "freshReflection", changed ? 11 : 7, contract.getMethod("value").invoke(receiver));
        check.eq(prefix + "cachedArrayReflection", changed ? 7 : 6, sum.invoke(receiver, new int[]{2, 4}));
        try { check.eq(prefix + "cachedHandle", changed ? 11 : 7, handle.invokeWithArguments(receiver)); }
        catch (Throwable error) { throw new AssertionError("cached handle", error); }
        check.eq(prefix + "fieldIdentity", true, field.get(null) == token && contract.getField("TOKEN").get(null) == token);
        check.eq(prefix + "peerFieldIdentity", true, peerField.get(peer) == peerToken && peer.token == peerToken);
        check.eq(prefix + "peerCall", changed ? 13 : 3, peer.value());
        check.eq(prefix + "marker", 31, receiver.marker);
        check.eq(prefix + "receiverClass", true, receiver.getClass() == MemberDescriptorFixture.Implementation.class);
        if (late == null && changed) late = Class.forName(
                "com.github.ytlog.artdcevm.fixture.MemberDescriptorFixture$LateCalls").getMethod("call", contract);
        if (late != null) check.eq(prefix + "lateCall", changed ? 64 : 52, late.invoke(null, receiver));
    }
}
