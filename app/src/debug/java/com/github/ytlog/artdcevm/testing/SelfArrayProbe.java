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
    private final Method[][] parentViews = new Method[2][3];
    private final MethodHandle[][] parentHandles = new MethodHandle[2][3];
    private final Class<?>[][] parentArrays = new Class<?>[2][3];

    public SelfArrayProbe(Class<?> type) throws Exception {
        this(type, false);
    }

    public SelfArrayProbe(Class<?> type, boolean parents) throws Exception {
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
        if (parents) {
            Class<?>[] types = {java.io.Serializable.class, Cloneable.class};
            String[] names = {"serializableView", "cloneableView"};
            for (int p = 0; p < types.length; ++p) {
                for (int rank = 0; rank < 3; ++rank) {
                    parentArrays[p][rank] = Array.newInstance(rank == 0 ? types[p] : parentArrays[p][rank - 1], 0).getClass();
                    String name = names[p].replace("View", rank == 0 ? "View" : rank == 1 ? "MatrixView" : "CubeView");
                    parentViews[p][rank] = type.getMethod(name, arrays[rank]);
                    parentHandles[p][rank] = MethodHandles.publicLookup().unreflect(parentViews[p][rank]);
                }
            }
        }
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

    /** Parent and covariant-array identities captured before or after conversion. */
    public Map<String, Object> observeParents() throws Exception {
        Map<String, Object> out = new LinkedHashMap<>();
        out.put("interface", type.isInterface());
        out.put("superclass", type.getSuperclass() == (type.isInterface() ? null : Object.class));
        Class<?>[] parents = {java.io.Serializable.class, Cloneable.class};
        String[] names = {"serializable", "cloneable"};
        out.put("directParents", type.getInterfaces().length);
        for (int p = 0; p < parents.length; ++p) {
            final int index = p;
            String prefix = names[p] + ".";
            out.put(prefix + "direct", type.getInterfaces().length == 1 && type.getInterfaces()[0] == parents[p]);
            out.put(prefix + "assignable", parents[p].isAssignableFrom(type));
            for (int rank = 0; rank < 3; ++rank) {
                final int r = rank;
                out.put(prefix + "assignable" + (rank + 1), parentArrays[p][rank].isAssignableFrom(arrays[rank]));
                out.put(prefix + "instance" + (rank + 1), parentArrays[p][rank].isInstance(retained[rank]));
                out.put(prefix + "cast" + (rank + 1), result(() -> parentArrays[index][r].cast(retained[r]) == retained[r]));
                String suffix = rank == 0 ? "" : Integer.toString(rank + 1);
                String method = names[p] + (rank == 0 ? "View" : rank == 1 ? "MatrixView" : "CubeView");
                out.put(prefix + "cachedOwner" + suffix, parentViews[p][rank].getDeclaringClass() == type
                        && parentViews[p][rank].getReturnType() == parentArrays[p][rank]
                        && parentViews[p][rank].getParameterTypes()[0] == arrays[rank]);
                out.put(prefix + "cachedView" + suffix, result(() -> parentViews[index][r].invoke(null, retained[r]) == retained[r]));
                out.put(prefix + "freshView" + suffix, result(() -> type.getMethod(method, arrays[r]).invoke(null, retained[r]) == retained[r]));
                out.put(prefix + "handleView" + suffix, result(() -> parentHandles[index][r].invokeWithArguments(new Object[]{retained[r]}) == retained[r]));
            }
        }
        return out;
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
        if (parentViews[0][0] != null)
            for (Map.Entry<String, Object> entry : observeParents().entrySet())
                out.put("parents." + entry.getKey(), entry.getValue());
        return out;
    }
}
