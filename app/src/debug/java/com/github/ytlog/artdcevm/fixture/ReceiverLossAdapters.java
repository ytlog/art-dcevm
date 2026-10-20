package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** The same prelinked adapter graphs run on ART and the fixed JBR oracle. */
public final class ReceiverLossAdapters {
    private static final Class<?> C = ReceiverLossFixture.Contract.class;
    private static final MethodHandles.Lookup LOOKUP = MethodHandles.publicLookup();
    private static final StringBuilder TRACE = new StringBuilder();
    private static final ReceiverLossFixture.Contract REPLACEMENT = new ReceiverLossFixture.Contract() {};
    private final List<Case> cases = new ArrayList<>();
    private static final class Case {
        final String name;
        final MethodHandle handle;
        final Object[] args;
        Case(String name, MethodHandle handle, Object... args) {
            this.name = name; this.handle = handle; this.args = args;
        }
    }
    private void add(String name, MethodHandle handle, Object... args) {
        cases.add(new Case(name, handle, args));
    }
    private static MethodHandle method(String name, Class<?> result, Class<?>... args) throws Exception {
        return LOOKUP.findStatic(ReceiverLossAdapters.class, name, MethodType.methodType(result, args));
    }
    public static int constant() { TRACE.append('K'); return 43; }
    public static int returned(int value) { TRACE.append('R'); return value + 1; }
    public static int constantArgument(ReceiverLossFixture.Contract ignored) { TRACE.append('K'); return 43; }
    public static int recoverFilter(IllegalStateException ignored) { TRACE.append('H'); return 39; }
    public static int filtered(int value) { TRACE.append('F'); return value + 2; }
    public static int fail(int value) { TRACE.append('X'); throw new IllegalStateException("filter"); }
    public static boolean yes(ReceiverLossFixture.Contract ignored) { TRACE.append('T'); return true; }
    public static boolean no(ReceiverLossFixture.Contract ignored) { TRACE.append('T'); return false; }
    public static void visit(ReceiverLossFixture.Contract ignored) { TRACE.append('V'); }
    public static void visitText(String ignored) { TRACE.append('V'); }
    public static ReceiverLossFixture.Contract same(ReceiverLossFixture.Contract value) { TRACE.append('S'); return value; }
    public static ReceiverLossFixture.Contract replace(ReceiverLossFixture.Contract ignored) { TRACE.append('P'); return REPLACEMENT; }
    public static int recover(IncompatibleClassChangeError ignored, ReceiverLossFixture.Contract receiver) { TRACE.append('H'); return 31; }

    public ReceiverLossAdapters(ReceiverLossFixture.Contract receiver) throws Exception {
        MethodHandle value = LOOKUP.findVirtual(C, "value", MethodType.methodType(int.class));
        MethodHandle arguments = LOOKUP.findVirtual(C, "arguments", MethodType.methodType(int.class, int.class, String.class));
        MethodHandle bound = value.bindTo(receiver);
        MethodHandle constant = method("constant", int.class);
        MethodHandle returning = method("returned", int.class, int.class);
        MethodHandle filter = method("filtered", int.class, int.class);
        MethodHandle failing = method("fail", int.class, int.class);
        MethodHandle ignored = MethodHandles.dropArguments(constant, 0, C);
        add("Inserted", MethodHandles.insertArguments(value, 0, receiver));
        add("InsertedNull", MethodHandles.insertArguments(value, 0, (Object) null));
        add("UnusedBound", ignored.bindTo(receiver));
        add("ReturnOutside", MethodHandles.filterReturnValue(bound, returning));
        add("ReturnInside", MethodHandles.filterReturnValue(value, returning).bindTo(receiver));
        add("FilterInside", MethodHandles.filterArguments(arguments, 1, filter).bindTo(receiver), 5, "abc");
        add("FilterThrows", MethodHandles.filterArguments(arguments, 1, failing).bindTo(receiver), 5, "abc");
        add("FilterBadReference", MethodHandles.filterArguments(arguments, 1, filter).bindTo(receiver), 5, new Object());
        add("FilterNullPrimitive", MethodHandles.filterArguments(arguments, 1, filter).bindTo(receiver), null, "abc");
        add("FilterReceiver", MethodHandles.filterArguments(value, 0, method("same", C, C)).bindTo(receiver));
        add("FilterReplacement", MethodHandles.filterArguments(value, 0, method("replace", C, C)).bindTo(receiver));
        add("GuardTrue", MethodHandles.guardWithTest(method("yes", boolean.class, C), value, ignored).bindTo(receiver));
        add("GuardFalse", MethodHandles.guardWithTest(method("no", boolean.class, C), value, ignored).bindTo(receiver));
        add("CatchInside", MethodHandles.catchException(value, IncompatibleClassChangeError.class,
                method("recover", int.class, IncompatibleClassChangeError.class, C)).bindTo(receiver));
        add("FoldInside", MethodHandles.foldArguments(value, method("visit", void.class, C)).bindTo(receiver));
        add("CollectInside", MethodHandles.collectArguments(value, 1, method("visitText", void.class, String.class)).bindTo(receiver), "text");
        MethodHandle permuted = MethodHandles.permuteArguments(arguments,
                MethodType.methodType(int.class, C, String.class, int.class), 0, 2, 1).bindTo(receiver);
        add("Permuted", permuted, "abc", 5);
        add("PermutedBadReference", permuted, new Object(), 5);
        add("SpreadInside", arguments.asSpreader(Object[].class, 2).bindTo(receiver), (Object) new Object[]{5, "abc"});
        add("SpreadWrongLength", arguments.asSpreader(Object[].class, 2).bindTo(receiver), (Object) new Object[]{5});
        add("SpreadBadReference", arguments.asSpreader(Object[].class, 2).bindTo(receiver), (Object) new Object[]{5, new Object()});
        add("AsTypeInside", value.asType(MethodType.methodType(Object.class, C)).bindTo(receiver));
        add("ExplicitInside", MethodHandles.explicitCastArguments(value, MethodType.methodType(long.class, C)).bindTo(receiver));
        add("InsertedGraph", MethodHandles.insertArguments(MethodHandles.filterReturnValue(value, returning), 0, receiver));
        add("NestedBindings", MethodHandles.dropArguments(value, 1, String.class).bindTo(receiver).bindTo("ignored"));
        add("CollectReceiver", MethodHandles.collectArguments(value, 0, MethodHandles.constant(C, receiver)));
        add("FoldReceiver", MethodHandles.foldArguments(value, MethodHandles.constant(C, receiver)));
        add("ExplicitObject", MethodHandles.explicitCastArguments(value, MethodType.methodType(int.class, Object.class)), receiver);
        add("StaticBound", method("constantArgument", int.class, C).bindTo(receiver));
        add("CatchOutside", MethodHandles.catchException(
                MethodHandles.filterReturnValue(value, returning).bindTo(receiver), IncompatibleClassChangeError.class,
                MethodHandles.dropArguments(method("constant", int.class), 0, IncompatibleClassChangeError.class)));
        add("CatchFilter", MethodHandles.catchException(
                MethodHandles.filterArguments(arguments, 1, failing).bindTo(receiver), IllegalStateException.class,
                method("recoverFilter", int.class, IllegalStateException.class)), 5, "abc");
        add("GuardNull", MethodHandles.guardWithTest(method("no", boolean.class, C), value, ignored).bindTo(null));
        add("ExplicitBoundObject", MethodHandles.explicitCastArguments(value, MethodType.methodType(int.class, Object.class)).bindTo(receiver));
        add("AsTypeBoundObject", value.asType(MethodType.methodType(int.class, Object.class)).bindTo(receiver));
        MethodHandle exact = MethodHandles.exactInvoker(value.type());
        MethodHandle generic = MethodHandles.invoker(value.type());
        add("ExactInvoker", MethodHandles.insertArguments(exact, 0, value, receiver));
        add("GenericInvoker", MethodHandles.insertArguments(generic, 0, value, receiver));
        add("SpreadInvoker", MethodHandles.spreadInvoker(value.type(), 0).bindTo(value), (Object) new Object[]{receiver});
        MethodHandle array = LOOKUP.findVirtual(C, "array", MethodType.methodType(int.class, Object[].class));
        add("CollectorInside", array.asFixedArity().asCollector(Object[].class, 2).bindTo(receiver), "a", "b");
        add("CollectorArity", array.asFixedArity().asCollector(Object[].class, 2).bindTo(receiver), "a");
        add("VarargsInside", array.bindTo(receiver).asVarargsCollector(Object[].class), "a", "b");
        add("VarargsEmpty", array.bindTo(receiver).asVarargsCollector(Object[].class));
        add("FixedNullArray", array.bindTo(receiver).asFixedArity(), (Object) null);
        add("AsTypeObject", value.asType(MethodType.methodType(int.class, Object.class)), receiver);
    }
    public static String expected(String name, boolean changed, int value) {
        String icce = "java.lang.IncompatibleClassChangeError";
        switch (name) {
            case "InsertedNull": return "java.lang.NullPointerException:";
            case "UnusedBound": case "StaticBound": return "43:K";
            case "CatchOutside": return changed ? "43:K" : (value + 1) + ":R";
            case "CatchFilter": return "39:XH";
            case "GuardFalse": case "GuardNull": return "43:TK";
            case "FilterReplacement": return "7:P";
            case "FilterThrows": return "java.lang.IllegalStateException:X";
            case "FilterBadReference": case "PermutedBadReference": case "SpreadBadReference":
                return "java.lang.ClassCastException:";
            case "FilterNullPrimitive": return "java.lang.NullPointerException:";
            case "SpreadWrongLength": return "java.lang.IllegalArgumentException:";
            case "ReturnOutside": case "ReturnInside": case "InsertedGraph":
                return changed ? icce + ":" : (value + 1) + ":R";
            case "FilterInside": return changed ? icce + ":F" : "10:F";
            case "FilterReceiver": return (changed ? icce : value) + ":S";
            case "GuardTrue": return (changed ? icce : value) + ":T";
            case "CatchInside": return changed ? "31:H" : value + ":";
            case "FoldInside": case "CollectInside": return (changed ? icce : value) + ":V";
            case "Permuted": case "SpreadInside": return (changed ? icce : 8) + ":";
            case "CollectorArity": return "java.lang.invoke.WrongMethodTypeException:";
            case "CollectorInside": case "VarargsInside": return (changed ? icce : 2) + ":";
            case "VarargsEmpty": return (changed ? icce : 0) + ":";
            case "FixedNullArray": return (changed ? icce : "java.lang.NullPointerException") + ":";
            case "AsTypeObject": case "AsTypeBoundObject": case "SpreadInvoker": return (changed ? "java.lang.ClassCastException" : value) + ":";
            case "Inserted": case "AsTypeInside": case "ExplicitInside": case "NestedBindings":
            case "CollectReceiver": case "FoldReceiver": case "ExplicitObject": case "ExplicitBoundObject":
            case "ExactInvoker": case "GenericInvoker": return (changed ? icce : value) + ":";
            default: throw new AssertionError("Missing adapter expectation: " + name);
        }
    }
    public Map<String, String> observations() {
        Map<String, String> result = new LinkedHashMap<>();
        for (Case item : cases) {
            TRACE.setLength(0);
            String value;
            try { value = String.valueOf(item.handle.invokeWithArguments(item.args)); }
            catch (Throwable error) { value = error.getClass().getName(); }
            result.put(item.name, value + ":" + TRACE);
        }
        return result;
    }
}
