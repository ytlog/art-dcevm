package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.ClassFinalSubject;
import com.github.ytlog.artdcevm.fixture.CopiedChild;
import com.github.ytlog.artdcevm.fixture.CopiedDefault;
import com.github.ytlog.artdcevm.fixture.CopiedParent;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.FieldSubject;
import com.github.ytlog.artdcevm.fixture.FinalFieldSubject;
import com.github.ytlog.artdcevm.fixture.ModifierPassive;
import com.github.ytlog.artdcevm.fixture.ModifierSubject;
import com.github.ytlog.artdcevm.fixture.MethodCaller;
import com.github.ytlog.artdcevm.fixture.MethodSubject;
import com.github.ytlog.artdcevm.fixture.AbstractMethodSubject;
import com.github.ytlog.artdcevm.fixture.AbstractMethodChild;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.InvocationTargetException;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Runs conformance through a separate agent's standard JVMTI entry point. */
public final class StructuralRedefineInstrumentation extends Instrumentation {
    private static final String REPORT_FILE = "structural-redefine.json";
    private final JSONObject report = new JSONObject();
    private final JSONArray scenarios = new JSONArray();
    private Context context;
    private int failures;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override public void onStart() {
        context = getTargetContext();
        boolean completed = false;
        try {
            report.put("schemaVersion", 1);
            report.put("uid", Process.myUid());
            report.put("pid", Process.myPid());
            report.put("device", new JSONObject().put("model", Build.MODEL)
                    .put("api", Build.VERSION.SDK_INT).put("release", Build.VERSION.RELEASE));
            report.put("scenarios", scenarios);
            writeReport(false);
            JSONObject initialization = new JSONObject().put("passed", false);
            report.put("initialization", initialization);
            writeReport(false);
            DcevmRuntime.initialize(context);
            initialization.put("runtimeInitialized", true);
            writeReport(false);
            StandardJvmtiClient.attach(context);
            initialization.put("clientAttached", true).put("passed", true)
                    .put("capabilities", DcevmRuntime.capabilities());
            writeReport(false);
            runScenarios();
            completed = true;
        } catch (Throwable error) {
            failures++;
            try {
                report.put("fatalError", error.toString());
            } catch (Exception ignored) {
                // The instrumentation result below still reports an incomplete run.
            }
        }
        Bundle result = new Bundle();
        result.putString("reportFile", "files/" + REPORT_FILE);
        result.putBoolean("completed", completed);
        result.putInt("failures", failures);
        try {
            writeReport(completed);
        } catch (Exception error) {
            completed = false;
            result.putBoolean("completed", false);
            result.putString("reportError", error.toString());
        }
        finish(completed && failures == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }

    private void runScenarios() throws Exception {
        Evolving evolving = new Evolving(10);
        runScenario("body", 0, scenario -> {
            scenario.check("computeBefore", 11, evolving.compute());
            scenario.redefine(new Class<?>[]{Evolving.class}, "body");
            scenario.check("oldCompute", 12, evolving.compute());
            scenario.check("newCompute", 12, new Evolving(10).compute());
        });
        runScenario("add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "add");
            checkAddedDefinition(scenario, evolving);
        });
        String addedSchema = memberSchema(Evolving.class);
        Method deletedAdded = Evolving.class.getDeclaredMethod("addedMethod");
        runScenario("method-delete", 0, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "method-delete");
            scenario.check("oldCompute", 14, evolving.compute());
            scenario.check("newCompute", 14, new Evolving(10).compute());
            scenario.check("oldValue", 10, evolving.value);
            scenario.check("staticCounter", 7, Evolving.counter);
            scenario.expectError("removedLookup", NoSuchMethodException.class,
                    () -> Evolving.class.getDeclaredMethod("addedMethod"));
            scenario.expectReflectionError("cachedMethod",
                    () -> deletedAdded.invoke(evolving));
        });
        runScenario("method-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "add");
            checkAddedDefinition(scenario, evolving);
        });
        runScenario("hierarchy", 66, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "hierarchy");
            scenario.check("superclass", Object.class.getName(), Evolving.class.getSuperclass().getName());
            scenario.check("interfaceCount", 0, Evolving.class.getInterfaces().length);
            scenario.check("unchangedMembers", addedSchema, memberSchema(Evolving.class));
            checkAddedDefinition(scenario, evolving);
        });

        ModifierSubject modifier = new ModifierSubject();
        ModifierPassive passive = new ModifierPassive();
        FieldSubject fields = new FieldSubject();
        String originalModifierSchema = memberSchema(ModifierSubject.class);
        String originalFieldSchema = memberSchema(FieldSubject.class);
        String originalFieldValues = fieldValues(fields);
        Object originalObject = fields.o;
        Object originalStaticObject = FieldSubject.so;
        runScenario("atomic-modifier-public-field-delete", 64, scenario -> {
            scenario.check("modifierBefore", 42, modifier.throughPublic());
            scenario.check("passiveBefore", 42, passive.throughPublic());
            scenario.expectRejectionBy(FieldSubject.class);
            scenario.redefine(new Class<?>[]{ModifierSubject.class, FieldSubject.class},
                    "modifier-public", "field-delete");
            scenario.check("unchangedModifierMembers", originalModifierSchema, memberSchema(ModifierSubject.class));
            scenario.check("modifierThroughPublic", 42, modifier.throughPublic());
            scenario.check("passiveThroughPublic", 42, passive.throughPublic());
            scenario.check("modifierFinalizable", 29, modifier.finalizable());
            scenario.check("passiveFinalizable", 29, passive.finalizable());
            checkFieldDefinition(scenario, fields, originalFieldSchema, originalFieldValues,
                    originalObject, originalStaticObject);
        });
        runScenario("modifier-public", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ModifierSubject.class}, "modifier-public");
            checkPublicModifiers(scenario, modifier, passive);
            scenario.check("computeFlags", Modifier.PUBLIC,
                    ModifierSubject.class.getDeclaredMethod("compute").getModifiers());
            scenario.check("finalizableFlags", Modifier.PUBLIC,
                    ModifierSubject.class.getDeclaredMethod("finalizable").getModifiers());
            scenario.check("modifierFinalizable", 29, modifier.finalizable());
            scenario.check("passiveFinalizable", 29, passive.finalizable());
        });
        runScenario("modifier-final", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ModifierSubject.class}, "modifier-final");
            checkPublicModifiers(scenario, modifier, passive);
            scenario.check("computeFlags", Modifier.PUBLIC,
                    ModifierSubject.class.getDeclaredMethod("compute").getModifiers());
            scenario.check("finalizableFlags", Modifier.PUBLIC | Modifier.FINAL,
                    ModifierSubject.class.getDeclaredMethod("finalizable").getModifiers());
            scenario.check("modifierFinalizable", 30, modifier.finalizable());
            scenario.check("passiveFinalizable", 30, passive.finalizable());
        });
        runScenario("modifier-sync", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ModifierSubject.class}, "modifier-sync");
            checkPublicModifiers(scenario, modifier, passive);
            scenario.check("computeFlags", Modifier.PUBLIC | Modifier.SYNCHRONIZED,
                    ModifierSubject.class.getDeclaredMethod("compute").getModifiers());
            scenario.check("finalizableFlags", Modifier.PUBLIC,
                    ModifierSubject.class.getDeclaredMethod("finalizable").getModifiers());
            scenario.check("modifierFinalizable", 29, modifier.finalizable());
            scenario.check("passiveFinalizable", 29, passive.finalizable());
        });

        FinalFieldSubject finalFields = new FinalFieldSubject();
        finalFields.value = 61;
        FinalFieldSubject.staticValue = 72;
        runScenario("modifier-field-final", 0, scenario -> {
            scenario.check("oldValueBefore", 61, finalFields.read());
            scenario.check("staticValueBefore", 72, FinalFieldSubject.readStatic());
            scenario.redefine(new Class<?>[]{FinalFieldSubject.class}, "modifier-field-final");
            Field value = FinalFieldSubject.class.getDeclaredField("value");
            Field staticValue = FinalFieldSubject.class.getDeclaredField("staticValue");
            scenario.check("valueFlags", Modifier.PUBLIC | Modifier.FINAL, value.getModifiers());
            scenario.check("staticValueFlags", Modifier.PUBLIC | Modifier.STATIC | Modifier.FINAL,
                    staticValue.getModifiers());
            scenario.check("oldValue", 61, value.getInt(finalFields));
            scenario.check("oldRead", 61, finalFields.read());
            scenario.check("staticValue", 72, staticValue.getInt(null));
            scenario.check("staticRead", 72, FinalFieldSubject.readStatic());
            scenario.check("newConstructorValue", 41, new FinalFieldSubject().read());
        });
        for (String variant : new String[]{"field-delete", "field-types"}) {
            runScenario(variant, 64, scenario -> {
                scenario.redefine(new Class<?>[]{FieldSubject.class}, variant);
                checkFieldDefinition(scenario, fields, originalFieldSchema, originalFieldValues,
                        originalObject, originalStaticObject);
            });
        }
        ClassFinalSubject classFinal = new ClassFinalSubject();
        int originalClassFlags = ClassFinalSubject.class.getModifiers();
        String originalClassSchema = memberSchema(ClassFinalSubject.class);
        runScenario("class-final", 70, scenario -> {
            scenario.redefine(new Class<?>[]{ClassFinalSubject.class}, "class-final");
            scenario.check("classFlags", originalClassFlags, ClassFinalSubject.class.getModifiers());
            scenario.check("unchangedMembers", originalClassSchema, memberSchema(ClassFinalSubject.class));
            scenario.check("oldValue", 29, classFinal.value());
            scenario.check("newValue", 29, new ClassFinalSubject().value());
        });
        runMethodScenarios(fields, originalFieldSchema, originalFieldValues,
                originalObject, originalStaticObject);
        runCopiedDefaultScenario();
    }

    // A loaded subclass owns a copied interface default until its superclass
    // gains a public implementation; the re-linked subclass then has no copy.
    private void runCopiedDefaultScenario() throws Exception {
        CopiedChild child = new CopiedChild();
        CopiedDefault viaInterface = child;
        runScenario("copied-default-add", 0, scenario -> {
            scenario.check("copiedBefore", 91, viaInterface.copiedValue());
            scenario.redefine(new Class<?>[]{CopiedParent.class}, "copied-default-add");
            scenario.check("oldChildInterface", 92, viaInterface.copiedValue());
            scenario.check("oldChildVirtual", 92, child.copiedValue());
            scenario.check("newChild", 92, new CopiedChild().copiedValue());
            scenario.check("parentValue", 90, child.parentValue());
        });
    }

    private void runMethodScenarios(FieldSubject fields, String fieldSchema, String values,
            Object object, Object staticObject) throws Exception {
        MethodSubject subject = new MethodSubject();
        subject.value = 60;
        subject.keep = 70;
        MethodSubject.staticKeep = 80;
        Object reference = subject.object;
        Object staticReference = MethodSubject.staticObject;
        MethodTargets targets = new MethodTargets();
        String schema = memberSchema(MethodSubject.class);
        runScenario("atomic-method-retire-field-types", 64, scenario -> {
            checkMethodBaseline(scenario, subject, targets);
            scenario.expectRejectionBy(FieldSubject.class);
            scenario.redefine(new Class<?>[]{MethodSubject.class, FieldSubject.class},
                    "methods-delete", "field-types");
            scenario.check("unchangedMethodMembers", schema, memberSchema(MethodSubject.class));
            scenario.check("unchangedStaticCall", 82, MethodCaller.callStatic());
            scenario.check("unchangedVirtualCall", 63, MethodCaller.callVirtual(subject));
            scenario.check("unchangedSignatureCall", 64, MethodCaller.callSignature(subject));
            checkFieldDefinition(scenario, fields, fieldSchema, values, object, staticObject);
        });
        runScenario("native-method-delete", 67, scenario -> {
            scenario.redefine(new Class<?>[]{MethodSubject.class}, "methods-native-delete");
            scenario.check("unchangedMembers", schema, memberSchema(MethodSubject.class));
            scenario.check("nativeStillDeclared", true,
                    Modifier.isNative(MethodSubject.class.getDeclaredMethod("nativeValue").getModifiers()));
            scenario.check("unchangedDirectCall", 61, subject.throughDirect());
            scenario.check("unchangedVirtualCall", 63, MethodCaller.callVirtual(subject));
            checkMethodFields(scenario, subject, reference, staticReference);
        });
        runScenario("constructor-delete", 67, scenario -> {
            scenario.redefine(new Class<?>[]{MethodSubject.class}, "methods-constructor-delete");
            scenario.check("unchangedMembers", schema, memberSchema(MethodSubject.class));
            scenario.check("constructorStillDeclared", true,
                    MethodSubject.class.getDeclaredConstructor() != null);
            scenario.check("unchangedDirectCall", 61, subject.throughDirect());
            checkMethodFields(scenario, subject, reference, staticReference);
        });
        AbstractMethodChild abstractChild = new AbstractMethodChild();
        String abstractSchema = memberSchema(AbstractMethodSubject.class);
        runScenario("abstract-method-delete", 67, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractMethodSubject.class}, "methods-abstract-delete");
            scenario.check("unchangedMembers", abstractSchema, memberSchema(AbstractMethodSubject.class));
            scenario.check("abstractStillDeclared", true, Modifier.isAbstract(
                    AbstractMethodSubject.class.getDeclaredMethod("abstractValue").getModifiers()));
            scenario.check("oldConcreteValue", 17, abstractChild.concreteValue());
            scenario.check("oldAbstractValue", 19, abstractChild.abstractValue());
            scenario.check("newConcreteValue", 17, new AbstractMethodChild().concreteValue());
            scenario.check("staticMarker", 17, AbstractMethodSubject.marker);
        });
        for (int cycle = 0; cycle < 2; cycle++) {
            final MethodTargets cached = cycle == 0 ? targets : new MethodTargets();
            String suffix = cycle == 0 ? "" : "-again";
            runScenario("methods-retire" + suffix, 0, scenario -> {
                checkMethodBaseline(scenario, subject, cached);
                ActiveMethod active = new ActiveMethod(subject);
                try {
                    scenario.check("activeMethodEntered", true, active.start());
                    scenario.redefine(new Class<?>[]{MethodSubject.class}, "methods-delete");
                    System.gc();
                    active.finish();
                    scenario.check("activeMethodCompleted", true, !active.thread.isAlive());
                    scenario.check("activeMethodError", "", active.error);
                    scenario.check("activeMethodOldBody", 69, active.value);
                } finally {
                    active.finish();
                }
                scenario.expectError("removedActiveMethodLookup", NoSuchMethodException.class,
                        () -> MethodSubject.class.getDeclaredMethod("waitThenRetire",
                                CountDownLatch.class, CountDownLatch.class));
                scenario.expectError("cachedDirectCall", NoSuchMethodError.class, subject::throughDirect);
                scenario.expectError("cachedStaticCall", NoSuchMethodError.class, MethodCaller::callStatic);
                scenario.expectError("cachedVirtualCall", NoSuchMethodError.class,
                        () -> MethodCaller.callVirtual(subject));
                scenario.expectError("cachedSignatureCall", NoSuchMethodError.class,
                        () -> MethodCaller.callSignature(subject));
                cached.checkRetired(scenario, subject, true);
                System.gc();
                scenario.expectReflectionError("cachedReflectionAfterGc",
                        () -> cached.methods[2].invoke(subject));
                scenario.expectError("cachedHandleAfterGc", NoSuchMethodError.class,
                        () -> cached.invokeHandle(2, subject));
                scenario.expectError("cachedJniAfterGc", NoSuchMethodError.class,
                        () -> StandardJvmtiClient.callCachedMethod(subject, 2));
                scenario.check("inheritedVirtualDispatch", 71, MethodCaller.callParent(subject));
                scenario.check("inheritedInterfaceDispatch", 81, MethodCaller.callInterface(subject));
                scenario.check("newInheritedVirtualDispatch", 71, MethodCaller.callParent(new MethodSubject()));
                scenario.check("newInheritedInterfaceDispatch", 81, MethodCaller.callInterface(new MethodSubject()));
                Method changed = MethodSubject.class.getDeclaredMethod("signatureValue", long.class);
                scenario.check("newSignatureReturnType", "long", changed.getReturnType().getName());
                scenario.check("newSignatureOldInstance", 164L, changed.invoke(subject, 4L));
                scenario.check("newSignatureNewInstance", 124L, changed.invoke(new MethodSubject(), 4L));
                checkMethodFields(scenario, subject, reference, staticReference);
                checkRetiredInThreadInterpreter(scenario, subject, cached);
            });
            runScenario("methods-restore" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{MethodSubject.class}, "methods-original");
                scenario.check("restoredStaticCall", 82, MethodCaller.callStatic());
                scenario.check("restoredDirectCall", 61, subject.throughDirect());
                scenario.check("restoredVirtualCall", 63, MethodCaller.callVirtual(subject));
                scenario.check("restoredSignatureCall", 64, MethodCaller.callSignature(subject));
                scenario.check("restoredParentCall", 64, MethodCaller.callParent(subject));
                scenario.check("restoredInterfaceCall", 65, MethodCaller.callInterface(subject));
                scenario.check("freshReflection", 63,
                        MethodSubject.class.getDeclaredMethod("virtualValue").invoke(subject));
                scenario.expectError("removedReplacementSignature", NoSuchMethodException.class,
                        () -> MethodSubject.class.getDeclaredMethod("signatureValue", long.class));
                cached.checkRetired(scenario, subject, false);
                checkMethodFields(scenario, subject, reference, staticReference);
            });
        }
    }

    private void checkRetiredInThreadInterpreter(Scenario scenario, MethodSubject subject,
            MethodTargets cached) throws Exception {
        long before = StandardJvmtiClient.singleStepCount();
        int enabled = StandardJvmtiClient.setThreadSingleStep(true);
        try {
            scenario.check("threadSingleStepEnable", 0, enabled);
            scenario.expectError("threadInterpreterDirect", NoSuchMethodError.class, subject::throughDirect);
            scenario.expectError("threadInterpreterStatic", NoSuchMethodError.class, MethodCaller::callStatic);
            scenario.expectError("threadInterpreterVirtual", NoSuchMethodError.class,
                    () -> MethodCaller.callVirtual(subject));
            scenario.expectError("threadInterpreterHandleExact", NoSuchMethodError.class,
                    () -> cached.invokeHandle(2, subject, 1));
            scenario.expectError("threadInterpreterHandleInvoke", NoSuchMethodError.class,
                    () -> cached.invokeHandle(0, subject, 2));
            scenario.expectError("threadInterpreterJni", NoSuchMethodError.class,
                    () -> StandardJvmtiClient.callCachedMethod(subject, 0));
            scenario.expectError("threadInterpreterReflectedJni", NoSuchMethodError.class,
                    () -> StandardJvmtiClient.callReflectedMethod(cached.methods[2], subject, 2));
            scenario.expectReflectionError("threadInterpreterReflection",
                    () -> cached.methods[2].invoke(subject));
        } finally {
            scenario.check("threadSingleStepDisable", 0, StandardJvmtiClient.setThreadSingleStep(false));
        }
        long callbacks = StandardJvmtiClient.singleStepCount() - before;
        scenario.result.put("threadSingleStepCallbacks", callbacks);
        scenario.check("threadSingleStepCallbacksObserved", true, callbacks > 0);
    }

    private static final class ActiveMethod {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        final Thread thread;
        volatile int value;
        volatile String error = "";

        ActiveMethod(MethodSubject subject) {
            thread = new Thread(() -> {
                try {
                    value = subject.waitThenRetire(entered, release);
                } catch (Throwable failure) {
                    error = failure.getClass().getName();
                }
            }, "retired-method-frame");
        }

        boolean start() throws InterruptedException {
            thread.start();
            return entered.await(10, TimeUnit.SECONDS);
        }

        void finish() throws InterruptedException {
            release.countDown();
            thread.join(10000);
        }
    }

    private void checkMethodBaseline(Scenario scenario, MethodSubject subject,
            MethodTargets targets) throws Exception {
        targets.adapt(subject);
        // Warm caller resolutions and JIT code before each retirement.
        for (int count = 0; count < 12000; count++) {
            MethodCaller.callStatic();
            MethodCaller.callVirtual(subject);
            MethodCaller.callSignature(subject);
            MethodCaller.callParent(subject);
            MethodCaller.callInterface(subject);
            subject.throughDirect();
        }
        scenario.check("staticBefore", 82, MethodCaller.callStatic());
        scenario.check("directBefore", 61, subject.throughDirect());
        scenario.check("virtualBefore", 63, MethodCaller.callVirtual(subject));
        scenario.check("signatureBefore", 64, MethodCaller.callSignature(subject));
        scenario.check("parentBefore", 64, MethodCaller.callParent(subject));
        scenario.check("interfaceBefore", 65, MethodCaller.callInterface(subject));
        int[] expected = {61, 82, 63, 64};
        for (int index = 0; index < targets.methods.length; index++) {
            final int method = index;
            scenario.check("reflectionBefore" + index, expected[index],
                    targets.methods[index].invoke(index == 1 ? null : subject,
                            index == 3 ? new Object[]{4} : new Object[0]));
            scenario.check("handleBefore" + index, expected[index],
                    targets.invokeHandle(index, subject));
            scenario.check("handleExactBefore" + index, expected[index],
                    targets.invokeHandle(index, subject, 1));
            scenario.check("handleInvokeBefore" + index, expected[index],
                    targets.invokeHandle(index, subject, 2));
            scenario.check("handleBoundBefore" + index, expected[index],
                    targets.invokeHandle(index, subject, 3));
            scenario.check("handleAdaptedBefore" + index, expected[index],
                    targets.invokeHandle(index, subject, 4));
            scenario.check("jniBefore" + index, expected[index],
                    StandardJvmtiClient.callCachedMethod(subject, method));
            scenario.check("reflectedJniBefore" + index, expected[index],
                    StandardJvmtiClient.callReflectedMethod(targets.methods[index], subject, method));
            for (int form = 1; form <= 2; form++) {
                String suffix = form == 1 ? "A" : "V";
                scenario.check("jni" + suffix + "Before" + index, expected[index],
                        StandardJvmtiClient.callCachedMethodVariant(subject, method, form));
                scenario.check("reflectedJni" + suffix + "Before" + index, expected[index],
                        StandardJvmtiClient.callReflectedMethodVariant(
                                targets.methods[index], subject, method, form));
            }
        }
    }

    private void checkMethodFields(Scenario scenario, MethodSubject subject,
            Object object, Object staticObject) throws Exception {
        scenario.check("oldValue", 60, subject.value);
        scenario.check("oldKeep", 70L, subject.keep);
        scenario.check("staticKeep", 80, MethodSubject.staticKeep);
        scenario.check("instanceReferencePreserved", true, subject.object == object);
        scenario.check("staticReferencePreserved", true, MethodSubject.staticObject == staticObject);
        scenario.check("stableOldCall", 66, subject.stableValue());
        scenario.check("newConstructorValue", 20, new MethodSubject().value);
    }

    private static final class MethodTargets {
        final String[] names = {"directValue", "staticValue", "virtualValue", "signatureValue"};
        final Method[] methods = new Method[4];
        final MethodHandle[] handles = new MethodHandle[4];
        // Transformer handles invoke their direct target through an emulated
        // stack frame rather than invoke-polymorphic on it.
        final MethodHandle[] bound = new MethodHandle[4];
        final MethodHandle[] adapted = new MethodHandle[4];

        MethodTargets() throws Exception {
            for (int index = 0; index < names.length; index++) {
                methods[index] = MethodSubject.class.getDeclaredMethod(names[index],
                        index == 3 ? new Class<?>[]{int.class} : new Class<?>[0]);
                methods[index].setAccessible(true);
                handles[index] = MethodHandles.lookup().unreflect(methods[index]);
            }
            StandardJvmtiClient.cacheMethodIds(MethodSubject.class);
        }

        Object invokeHandle(int index, MethodSubject subject) throws Exception {
            return invokeHandle(index, subject, 0);
        }

        /** Builds the transformer handles while every target is still live. */
        void adapt(MethodSubject subject) {
            for (int index = 0; index < handles.length; index++) {
                bound[index] = index == 1
                        ? MethodHandles.dropArguments(handles[index], 0, MethodSubject.class).bindTo(subject)
                        : handles[index].bindTo(subject);
                adapted[index] = handles[index].asType(handles[index].type().changeReturnType(Object.class));
            }
        }

        Object invokeHandle(int index, MethodSubject subject, int form) throws Exception {
            try {
                if (form == 3) {
                    if (index == 3) return (int) bound[index].invokeExact(4);
                    return (int) bound[index].invokeExact();
                }
                if (form == 4) {
                    if (index == 1) return (Object) adapted[index].invokeExact();
                    if (index == 3) return (Object) adapted[index].invokeExact(subject, 4);
                    return (Object) adapted[index].invokeExact(subject);
                }
                if (form == 1) {
                    if (index == 1) return (int) handles[index].invokeExact();
                    if (index == 3) return (int) handles[index].invokeExact(subject, 4);
                    return (int) handles[index].invokeExact(subject);
                }
                if (form == 2) {
                    if (index == 1) return (int) handles[index].invoke();
                    if (index == 3) return (int) handles[index].invoke(subject, 4);
                    return (int) handles[index].invoke(subject);
                }
                if (index == 1) return handles[index].invokeWithArguments();
                if (index == 3) return handles[index].invokeWithArguments(subject, 4);
                return handles[index].invokeWithArguments(subject);
            } catch (Throwable error) {
                if (error instanceof Exception) throw (Exception) error;
                if (error instanceof Error) throw (Error) error;
                throw new RuntimeException(error);
            }
        }

        void checkRetired(Scenario scenario, MethodSubject subject, boolean checkLookup) throws Exception {
            for (int index = 0; index < methods.length; index++) {
                final int method = index;
                if (checkLookup) {
                    scenario.expectError("removedLookup" + index, NoSuchMethodException.class,
                            () -> MethodSubject.class.getDeclaredMethod(names[method],
                                    method == 3 ? new Class<?>[]{int.class} : new Class<?>[0]));
                    scenario.expectError("freshJniLookup" + index, NoSuchMethodError.class,
                            () -> StandardJvmtiClient.lookupMethodId(MethodSubject.class, method));
                }
                scenario.expectReflectionError("cachedReflection" + index,
                        () -> methods[method].invoke(method == 1 ? null : subject,
                                method == 3 ? new Object[]{4} : new Object[0]));
                scenario.expectError("cachedHandle" + index, NoSuchMethodError.class,
                        () -> invokeHandle(method, subject));
                scenario.expectError("cachedHandleExact" + index, NoSuchMethodError.class,
                        () -> invokeHandle(method, subject, 1));
                scenario.expectError("cachedHandleInvoke" + index, NoSuchMethodError.class,
                        () -> invokeHandle(method, subject, 2));
                scenario.expectError("cachedHandleBound" + index, NoSuchMethodError.class,
                        () -> invokeHandle(method, subject, 3));
                scenario.expectError("cachedHandleAdapted" + index, NoSuchMethodError.class,
                        () -> invokeHandle(method, subject, 4));
                scenario.expectError("cachedJni" + index, NoSuchMethodError.class,
                        () -> StandardJvmtiClient.callCachedMethod(subject, method));
                scenario.expectError("reflectedJni" + index, NoSuchMethodError.class,
                        () -> StandardJvmtiClient.callReflectedMethod(methods[method], subject, method));
                for (int form = 1; form <= 2; form++) {
                    final int callForm = form;
                    String suffix = form == 1 ? "A" : "V";
                    scenario.expectError("cachedJni" + suffix + index, NoSuchMethodError.class,
                            () -> StandardJvmtiClient.callCachedMethodVariant(subject, method, callForm));
                    scenario.expectError("reflectedJni" + suffix + index, NoSuchMethodError.class,
                            () -> StandardJvmtiClient.callReflectedMethodVariant(
                                    methods[method], subject, method, callForm));
                }
            }
        }
    }

    private void checkAddedDefinition(Scenario scenario, Evolving evolving) throws Exception {
        Field added = Evolving.class.getDeclaredField("added");
        Method addedMethod = Evolving.class.getDeclaredMethod("addedMethod");
        Evolving fresh = new Evolving(10);
        scenario.check("oldCompute", 13, evolving.compute());
        scenario.check("newCompute", 13, fresh.compute());
        scenario.check("addedType", "long", added.getType().getName());
        scenario.check("oldAdded", 0L, added.getLong(evolving));
        scenario.check("newAdded", 77L, added.getLong(fresh));
        scenario.check("oldAddedMethod", 110, addedMethod.invoke(evolving));
        scenario.check("newAddedMethod", 187, addedMethod.invoke(fresh));
        scenario.check("oldValue", 10, evolving.value);
        scenario.check("staticCounter", 7, Evolving.counter);
    }

    private void checkPublicModifiers(Scenario scenario, ModifierSubject modifier,
            ModifierPassive passive) throws Exception {
        for (String name : new String[]{"value", "packageValue"}) {
            scenario.check(name + "Flags", Modifier.PUBLIC,
                    ModifierSubject.class.getDeclaredField(name).getModifiers());
        }
        // compute's synchronized bit is checked separately in the synchronized variant.
        scenario.check("computePublic", true,
                Modifier.isPublic(ModifierSubject.class.getDeclaredMethod("compute").getModifiers()));
        scenario.check("packageComputeFlags", Modifier.PUBLIC,
                ModifierSubject.class.getDeclaredMethod("packageCompute").getModifiers());
        scenario.check("modifierThroughPublic", 43, modifier.throughPublic());
        scenario.check("passiveThroughPublic", 43, passive.throughPublic());
        scenario.check("packageCompute", 13,
                ModifierSubject.class.getDeclaredMethod("packageCompute").invoke(modifier));
        scenario.check("passiveSuperclass", ModifierSubject.class.getName(),
                passive.getClass().getSuperclass().getName());
        scenario.check("newPassiveThroughPublic", 43, new ModifierPassive().throughPublic());
    }

    private void checkFieldDefinition(Scenario scenario, FieldSubject fields, String schema,
            String values, Object object, Object staticObject) throws Exception {
        scenario.check("unchangedMembers", schema, memberSchema(FieldSubject.class));
        scenario.check("unchangedValues", values, fieldValues(fields));
        scenario.check("oldCompute", 41, fields.compute());
        scenario.check("newCompute", 41, new FieldSubject().compute());
        scenario.check("instanceReferencePreserved", true, fields.o == object);
        scenario.check("staticReferencePreserved", true, FieldSubject.so == staticObject);
    }

    private static String fieldValues(FieldSubject fields) {
        return fields.keep + ":" + FieldSubject.staticKeep + ":" + fields.z + ":" + fields.b
                + ":" + (int) fields.c + ":" + fields.s + ":" + fields.i + ":" + fields.j
                + ":" + fields.f + ":" + fields.d + ":" + FieldSubject.sz + ":" + FieldSubject.sb
                + ":" + (int) FieldSubject.sc + ":" + FieldSubject.ss + ":" + FieldSubject.si
                + ":" + FieldSubject.sj + ":" + FieldSubject.sf + ":" + FieldSubject.sd;
    }

    private static String memberSchema(Class<?> type) {
        List<String> members = new ArrayList<>();
        for (Field field : type.getDeclaredFields()) {
            members.add("field:" + field.getName() + ":" + field.getType().getName()
                    + ":" + field.getModifiers());
        }
        for (Method method : type.getDeclaredMethods()) {
            StringBuilder signature = new StringBuilder("method:").append(method.getName()).append('(');
            for (Class<?> parameter : method.getParameterTypes()) signature.append(parameter.getName()).append(';');
            members.add(signature.append("):").append(method.getReturnType().getName()).append(':')
                    .append(method.getModifiers()).toString());
        }
        Collections.sort(members);
        return members.toString();
    }

    private interface ScenarioBody {
        void run(Scenario scenario) throws Exception;
    }

    private interface ThrowingAction {
        void run() throws Throwable;
    }

    private void runScenario(String name, int expectedCode, ScenarioBody body) throws Exception {
        Scenario scenario = new Scenario(name, expectedCode);
        scenarios.put(scenario.result);
        writeReport(false);
        try {
            body.run(scenario);
        } catch (Throwable error) {
            scenario.passed = false;
            scenario.result.put("error", error.toString());
        }
        scenario.result.put("completed", true).put("passed", scenario.passed)
                .put("capabilities", DcevmRuntime.capabilities());
        if (!scenario.passed) failures++;
        writeReport(false);
    }

    private final class Scenario {
        final JSONObject result;
        final JSONArray checks = new JSONArray();
        final int expectedCode;
        Class<?> rejectedClass;
        boolean passed = true;

        Scenario(String name, int code) throws Exception {
            expectedCode = code;
            result = new JSONObject().put("name", name).put("expectedCode", code)
                    .put("completed", false).put("passed", false).put("checks", checks);
        }

        void redefine(Class<?>[] classes, String... variants) throws Exception {
            if (classes.length != variants.length) throw new IllegalArgumentException("Fixture batch mismatch");
            byte[][] dexes = new byte[classes.length][];
            JSONArray names = new JSONArray();
            for (int index = 0; index < classes.length; index++) {
                names.put(variants[index]);
                dexes[index] = readAsset(variants[index] + "/"
                        + classes[index].getName().replace('.', '/') + ".dex");
            }
            result.put("variants", names);
            writeReport(false);
            int actualCode = StandardJvmtiClient.redefine(classes, dexes);
            result.put("actualCode", actualCode);
            check("returnCode", expectedCode, actualCode);
            checkRejectionReason(classes);
            writeReport(false);
        }

        /** Names the batch member that the engine policy must reject. */
        void expectRejectionBy(Class<?> klass) {
            rejectedClass = klass;
        }

        // The engine resets lastRejection for each standard-entry transaction, so
        // an expected rejection must come from its policy for the right class.
        private void checkRejectionReason(Class<?>[] classes) throws Exception {
            String reason = DcevmRuntime.capabilities().optString("lastRejection");
            if (expectedCode == 0) {
                check("lastRejection", "", reason);
                return;
            }
            Class<?> rejected = rejectedClass != null ? rejectedClass : classes[0];
            String prefix = "L" + rejected.getName().replace('.', '/') + ";: ";
            result.put("lastRejection", reason);
            check("rejectedByPolicy", prefix, reason.startsWith(prefix) ? prefix : reason);
        }

        void check(String name, Object expected, Object actual) throws Exception {
            boolean matches = expected == null ? actual == null : expected.equals(actual);
            passed &= matches;
            checks.put(new JSONObject().put("name", name).put("expected", JSONObject.wrap(expected))
                    .put("actual", JSONObject.wrap(actual)).put("passed", matches));
        }

        void expectError(String name, Class<? extends Throwable> expected,
                ThrowingAction action) throws Exception {
            String actual = "completed without exception";
            try {
                action.run();
            } catch (Throwable error) {
                if (error instanceof InvocationTargetException && error.getCause() != null) {
                    error = error.getCause();
                }
                actual = error.getClass().getName();
            }
            check(name, expected.getName(), actual);
        }

        void expectReflectionError(String name, ThrowingAction action) throws Exception {
            String actual = "completed without exception";
            try {
                action.run();
            } catch (Throwable error) {
                actual = error.getClass().getName();
                if (error instanceof InvocationTargetException && error.getCause() != null) {
                    actual += ":" + error.getCause().getClass().getName();
                }
            }
            check(name, "java.lang.reflect.InvocationTargetException:java.lang.NoSuchMethodError", actual);
        }
    }

    private byte[] readAsset(String path) throws IOException {
        try (InputStream input = context.getAssets().open(path);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }

    private void writeReport(boolean completed) throws Exception {
        report.put("completed", completed).put("failures", failures);
        File temporary = new File(context.getFilesDir(), REPORT_FILE + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
        if (!temporary.renameTo(new File(context.getFilesDir(), REPORT_FILE))) {
            throw new IOException("Cannot publish instrumentation report");
        }
    }
}
