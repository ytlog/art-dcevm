package com.github.ytlog.artdcevm.testing;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.Map;

/** Shared unchanged with the fixed JBR oracle. Holds arrays, never target instances. */
public final class SelfArrayProbe {
    public final Class<?> type;
    private final Class<?>[] arrays = new Class<?>[3];
    private final Object[] retained = new Object[3];
    private final Method pack, first, put, matrix, cube, cast, store;
    private final MethodHandle handle;
    private final Field field;

    public SelfArrayProbe(Class<?> type) throws Exception {
        this.type = type;
        for (int i = 0; i < 3; ++i) {
            retained[i] = Array.newInstance(i == 0 ? type : arrays[i - 1], 1);
            arrays[i] = retained[i].getClass();
            if (i != 0) Array.set(retained[i], 0, retained[i - 1]);
        }
        pack = type.getMethod("pack", type);
        first = type.getMethod("first", arrays[0]);
        put = type.getMethod("put", arrays[0], type);
        matrix = type.getMethod("matrix", arrays[0]);
        cube = type.getMethod("cube", arrays[1]);
        cast = type.getMethod("arrayCast", Object.class);
        store = type.getMethod("unsafeStore", Object[].class, Object.class);
        handle = MethodHandles.publicLookup().unreflect(pack);
        field = type.getField("FIELD");
    }

    private interface Op { Object run() throws Throwable; }
    private static Object result(Op op) {
        try { return op.run(); }
        catch (Throwable error) {
            while (error instanceof InvocationTargetException) error = error.getCause();
            return error.getClass().getName();
        }
    }
    private boolean packed(Object value) {
        return value != null && value.getClass() == arrays[0] && Array.getLength(value) == 1
                && Array.get(value, 0) == null;
    }
    private static Object error(Op op) {
        return result(() -> { op.run(); return "returned"; });
    }

    public Map<String, Object> observe(SelfArrayProbe other) throws Exception {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("interface", type.isInterface());
        for (int i = 0; i < 3; ++i) {
            out.put("component" + (i + 1), arrays[i].getComponentType() == (i == 0 ? type : arrays[i - 1]));
            out.put("canonical" + (i + 1), Array.newInstance(i == 0 ? type : arrays[i - 1], 0).getClass() == arrays[i]);
            out.put("retained" + (i + 1), retained[i].getClass() == arrays[i]
                    && (i == 0 ? Array.get(retained[i], 0) == null : Array.get(retained[i], 0) == retained[i - 1]));
        }
        out.put("arrayLiteral", type.getMethod("arrayType").invoke(null) == arrays[0]);
        out.put("cachedField", field.getType() == arrays[0] && field.getDeclaringClass() == type);
        out.put("freshField", type.getField("FIELD").getType() == arrays[0]);
        out.put("fieldValue", field.get(null) == null);
        out.put("cachedOwner", pack.getDeclaringClass() == type && handle.type().parameterType(0) == type);
        out.put("cachedPack", result(() -> packed(pack.invoke(null, new Object[]{null}))));
        out.put("freshPack", result(() -> packed(type.getMethod("pack", type).invoke(null, new Object[]{null}))));
        out.put("handlePack", result(() -> packed(handle.invokeWithArguments(new Object[]{null}))));
        out.put("matrix", result(() -> {
            Object value = matrix.invoke(null, retained[0]);
            return value.getClass() == arrays[1] && Array.get(value, 0) == retained[0];
        }));
        out.put("cube", result(() -> {
            Object value = cube.invoke(null, retained[1]);
            return value.getClass() == arrays[2] && Array.get(value, 0) == retained[1];
        }));
        out.put("first", result(() -> first.invoke(null, retained[0]) == null));
        out.put("put", result(() -> { put.invoke(null, retained[0], null); return Array.get(retained[0], 0) == null; }));
        out.put("cast", result(() -> cast.invoke(null, retained[0]) == retained[0]));
        out.put("widen", result(() -> type.getMethod("widen", arrays[0]).invoke(null, retained[0]) == retained[0]));
        out.put("wrongCast", error(() -> cast.invoke(null, new Object())));
        out.put("wrongStore", error(() -> store.invoke(null, retained[0], new Object())));
        out.put("nullFirst", error(() -> first.invoke(null, new Object[]{null})));
        out.put("emptyFirst", error(() -> first.invoke(null, Array.newInstance(type, 0))));
        out.put("nullStore", error(() -> store.invoke(null, null, new Object())));
        out.put("otherName", type.getName().equals(other.type.getName()));
        out.put("otherLoader", type.getClassLoader() != other.type.getClassLoader());
        out.put("otherAssignable", !arrays[0].isAssignableFrom(other.arrays[0]));
        out.put("otherCast", error(() -> cast.invoke(null, other.retained[0])));
        out.put("otherArgument", error(() -> matrix.invoke(null, other.retained[0])));
        out.put("stable", type.getMethod("stable").invoke(null));
        return out;
    }
}
