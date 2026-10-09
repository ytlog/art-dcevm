package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.reflect.Field;

/** Installed APK fixture. Native changes only these mutable field flags and restores them. */
final class FinalPermissionOracle {
    static final class Target {
        public int instanceValue = 41;
        public static int staticValue = 43;
        public Object instanceReference;
        public static Object staticReference;
    }
    static final class Handles {
        final MethodHandle instanceGet, instanceSet, staticGet, staticSet;
        final VarHandle instanceVar, staticVar;
        final Field instanceField, staticField;
        final Field instanceReferenceField, staticReferenceField;
        final MethodHandle instanceReferenceGet, instanceReferenceSet, staticReferenceGet, staticReferenceSet;
        final VarHandle instanceReferenceVar, staticReferenceVar;
        Handles() throws Exception {
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            instanceField = Target.class.getField("instanceValue");
            staticField = Target.class.getField("staticValue");
            instanceGet = lookup.unreflectGetter(instanceField);
            instanceSet = lookup.unreflectSetter(instanceField);
            staticGet = lookup.unreflectGetter(staticField);
            staticSet = lookup.unreflectSetter(staticField);
            instanceVar = lookup.findVarHandle(Target.class, "instanceValue", int.class);
            staticVar = lookup.findStaticVarHandle(Target.class, "staticValue", int.class);
            instanceReferenceField = Target.class.getField("instanceReference");
            staticReferenceField = Target.class.getField("staticReference");
            instanceReferenceGet = lookup.unreflectGetter(instanceReferenceField);
            instanceReferenceSet = lookup.unreflectSetter(instanceReferenceField);
            staticReferenceGet = lookup.unreflectGetter(staticReferenceField);
            staticReferenceSet = lookup.unreflectSetter(staticReferenceField);
            instanceReferenceVar = lookup.findVarHandle(Target.class, "instanceReference", Object.class);
            staticReferenceVar = lookup.findStaticVarHandle(Target.class, "staticReference", Object.class);
        }
    }
    static Target create() { return new Target(); }
    static Handles prepareHandles() throws Exception { return new Handles(); }
    // A single foreign-owner writer covers both field event overload/storage paths.
    static void write(Target target, int value, Object object, boolean statik, boolean reference) {
        if (reference) {
            if (statik) Target.staticReference = object;
            else target.instanceReference = object;
            return;
        }
        if (statik) Target.staticValue = value;
        else target.instanceValue = value;
    }
    static int checkHandles(Handles handles, Target target, boolean statik, int value) throws Throwable {
        int result = 0;
        int mh = statik ? (int) handles.staticGet.invokeExact() : (int) handles.instanceGet.invokeExact(target);
        if (mh == value) result |= 1;
        int vh = statik ? (int) handles.staticVar.get() : (int) handles.instanceVar.get(target);
        if (vh == value) result |= 2;
        try {
            if (statik) handles.staticSet.invokeExact(value + 1);
            else handles.instanceSet.invokeExact(target, value + 1);
        } catch (IllegalAccessError expected) { result |= 4; }
        try {
            if (statik) handles.staticVar.set(value + 1);
            else handles.instanceVar.set(target, value + 1);
        } catch (UnsupportedOperationException expected) { result |= 8; }
        Field field = statik ? handles.staticField : handles.instanceField;
        if (field.getInt(statik ? null : target) == value) result |= 16;
        field.setAccessible(true);
        field.setInt(statik ? null : target, value + 1);
        if (field.getInt(statik ? null : target) == value + 1) result |= 32;
        return result;
    }
    static int checkReferenceHandles(Handles handles, Target target, boolean statik, Object value) throws Throwable {
        int result = 0;
        Object mh = statik ? (Object) handles.staticReferenceGet.invokeExact()
                          : (Object) handles.instanceReferenceGet.invokeExact(target);
        if (mh == value) result |= 1;
        Object vh = statik ? handles.staticReferenceVar.get() : handles.instanceReferenceVar.get(target);
        if (vh == value) result |= 2;
        Object next = handles;
        try {
            if (statik) handles.staticReferenceSet.invokeExact(next);
            else handles.instanceReferenceSet.invokeExact(target, next);
        } catch (IllegalAccessError expected) { result |= 4; }
        try {
            if (statik) handles.staticReferenceVar.set(next);
            else handles.instanceReferenceVar.set(target, next);
        } catch (UnsupportedOperationException expected) { result |= 8; }
        Field field = statik ? handles.staticReferenceField : handles.instanceReferenceField;
        if (field.get(statik ? null : target) == value) result |= 16;
        field.setAccessible(true);
        field.set(statik ? null : target, next);
        if (field.get(statik ? null : target) == next) result |= 32;
        return result;
    }

}
