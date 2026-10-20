package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.reflect.Constructor;
import java.util.LinkedHashMap;
import java.util.Map;

/** Shared ART/JBR observations; the independent oracle owns expectations. */
public final class ConstructorHistoryProbe {
    private interface Call { Object run() throws Throwable; }
    private final Class<?> type;
    private final Constructor<?> first, sibling, declared, declaredSibling, publicArray, declaredArray, late;
    private final MethodHandle handle;
    private Constructor<?> fresh, freshDeclared;
    public final String warmResult, declaredWarmResult;

    public ConstructorHistoryProbe(Class<?> type, String mode) throws Exception {
        this.type = type;
        first = type.getConstructor(int.class);
        sibling = type.getConstructor(int.class);
        declared = type.getDeclaredConstructor(int.class);
        declaredSibling = type.getDeclaredConstructor(int.class);
        publicArray = type.getConstructors()[0];
        declaredArray = type.getDeclaredConstructors()[0];
        handle = MethodHandles.publicLookup().unreflectConstructor(first);
        warmResult = mode.equals("Cold") ? "SKIPPED" : observe(() -> first.newInstance(
                mode.equals("Arity") ? new Object[0] : mode.equals("Type") ? new Object[]{"bad"} : new Object[]{1}));
        // Mirror the public-root cases independently: Cold warms only the
        // declared root; Warm leaves it cold; argument errors warm both roots.
        declaredWarmResult = mode.equals("Warm") ? "SKIPPED" : observe(() -> declared.newInstance(
                mode.equals("Arity") ? new Object[0] : mode.equals("Type") ? new Object[]{"bad"} : new Object[]{1}));
        late = type.getConstructor(int.class);
    }
    private static String observe(Call call) {
        try { call.run(); return "OK"; }
        catch (Throwable error) {
            return error.getClass().getName() + (error.getCause() == null ? "" : ":" + error.getCause().getClass().getName());
        }
    }
    public Map<String, String> observations(boolean changed, boolean abstractClass) throws Exception {
        Map<String, String> values = new LinkedHashMap<>();
        values.put("first", observe(() -> first.newInstance(1)));
        values.put("sibling", observe(() -> sibling.newInstance(1)));
        values.put("declared", observe(() -> declared.newInstance(1)));
        values.put("declaredSibling", observe(() -> declaredSibling.newInstance(1)));
        values.put("publicArray", observe(() -> publicArray.newInstance(1)));
        values.put("declaredArray", observe(() -> declaredArray.newInstance(1)));
        values.put("late", observe(() -> late.newInstance(1)));
        values.put("handle", observe(() -> handle.invokeWithArguments(1)));
        if (changed) {
            values.put("arity", observe(() -> first.newInstance()));
            values.put("type", observe(() -> first.newInstance("bad")));
            values.put("null", observe(() -> first.newInstance(new Object[]{null})));
            values.put("widen", observe(() -> first.newInstance((byte) 1)));
            values.put("declaredArity", observe(() -> declared.newInstance()));
            values.put("declaredType", observe(() -> declared.newInstance("bad")));
            if (abstractClass) {
                fresh = type.getConstructor(int.class);
                freshDeclared = type.getDeclaredConstructor(int.class);
                values.put("fresh", observe(() -> fresh.newInstance(1)));
                values.put("freshDeclared", observe(() -> freshDeclared.newInstance(1)));
                values.put("freshArity", observe(() -> fresh.newInstance()));
            }
        } else {
            if (fresh != null) {
                values.put("fresh", observe(() -> fresh.newInstance(1)));
                values.put("freshDeclared", observe(() -> freshDeclared.newInstance(1)));
            }
            values.put("newPublic", observe(() -> type.getConstructor(int.class).newInstance(1)));
            values.put("newDeclared", observe(() -> type.getDeclaredConstructor(int.class).newInstance(1)));
        }
        return values;
    }
}
