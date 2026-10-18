package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.InterfaceMethodFixture;
import com.github.ytlog.artdcevm.fixture.RetypeFixture;
import com.github.ytlog.artdcevm.fixture.SuperChainFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceEvolutionFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceKindFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceInvokeKindFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceParentFixture;
import com.github.ytlog.artdcevm.fixture.SuperBatchFixture;
import com.github.ytlog.artdcevm.fixture.ModifierFlagsFixture;
import com.github.ytlog.artdcevm.fixture.KindFixture;
import com.github.ytlog.artdcevm.fixture.InvokeKindFixture;
import com.github.ytlog.artdcevm.fixture.AbstractCaller;
import com.github.ytlog.artdcevm.fixture.AbstractCandidate;
import com.github.ytlog.artdcevm.fixture.ClinitSubject;
import com.github.ytlog.artdcevm.fixture.ConstructorCaller;
import com.github.ytlog.artdcevm.fixture.ConstructorSubject;
import com.github.ytlog.artdcevm.fixture.NativeCaller;
import com.github.ytlog.artdcevm.fixture.NativeSubject;
import com.github.ytlog.artdcevm.fixture.ClassFinalSubject;
import com.github.ytlog.artdcevm.fixture.CopiedChild;
import com.github.ytlog.artdcevm.fixture.CopiedDefault;
import com.github.ytlog.artdcevm.fixture.CopiedParent;
import com.github.ytlog.artdcevm.fixture.NestedInterfaceHost;
import com.github.ytlog.artdcevm.fixture.InterfaceBatchFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceOverlapFixture;
import com.github.ytlog.artdcevm.fixture.MultiInterfaceFixture;
import com.github.ytlog.artdcevm.fixture.PointerIdFixture;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.FieldCaller;
import com.github.ytlog.artdcevm.fixture.FieldSubject;
import com.github.ytlog.artdcevm.fixture.FinalFieldSubject;
import com.github.ytlog.artdcevm.fixture.InterfaceCaller;
import com.github.ytlog.artdcevm.fixture.InterfaceChild;
import com.github.ytlog.artdcevm.fixture.InterfaceSubject;
import com.github.ytlog.artdcevm.fixture.ModifierPassive;
import com.github.ytlog.artdcevm.fixture.ModifierSubject;
import com.github.ytlog.artdcevm.fixture.MethodCaller;
import com.github.ytlog.artdcevm.fixture.MethodSubject;
import com.github.ytlog.artdcevm.fixture.PreloadHookSubject;
import com.github.ytlog.artdcevm.fixture.PreloadSubject;
import com.github.ytlog.artdcevm.fixture.AccessCaller;
import com.github.ytlog.artdcevm.fixture.AccessSubject;
import com.github.ytlog.artdcevm.fixture.PrivateSelfFixture;
import com.github.ytlog.artdcevm.fixture.AccessCallerBase;
import com.github.ytlog.artdcevm.fixture.StorageCaller;
import com.github.ytlog.artdcevm.fixture.StorageSubject;
import com.github.ytlog.artdcevm.fixture.SuperChild;
import com.github.ytlog.artdcevm.fixture.SuperMiddle;
import com.github.ytlog.artdcevm.fixture.SuperSubject;
import com.github.ytlog.artdcevm.fixture.ThreadSubclassFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture;
import com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture;
import com.github.ytlog.artdcevm.fixture.NarrowedReferenceFixture;
import com.github.ytlog.artdcevm.fixture.ProxyFixture;
import com.github.ytlog.artdcevm.fixture.ProxyGroupFixture;
import com.github.ytlog.artdcevm.fixture.StaticNarrowingFixture;
import com.github.ytlog.artdcevm.fixture.ParentRemovalFixture;
import com.github.ytlog.artdcevm.fixture.ToolHost;
import com.github.ytlog.artdcevm.fixture.AbstractMethodSubject;
import com.github.ytlog.artdcevm.fixture.AbstractMethodChild;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.InvocationTargetException;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import com.github.ytlog.artdcevm.fixture.InterfaceStressTypes;
import com.github.ytlog.artdcevm.fixture.StressContract;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Iterator;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.IntSupplier;

/** Runs conformance through a separate agent's standard JVMTI entry point. */
public final class StructuralRedefineInstrumentation extends Instrumentation {
    // Calls per dispatch loop: enough to cross ART's JIT compile threshold so
    // later calls run compiled. Each failing call creates an exception, which is
    // costly on slower devices, so keep the count close to that threshold.
    private static final int DISPATCH_CALLS = 30_000;
    private static final String REPORT_FILE = "structural-redefine.json";
    private final JSONObject report = new JSONObject();
    private final JSONArray scenarios = new JSONArray();
    // runScenario never changes a result after marking it completed. Reusing
    // its JSON avoids serializing the entire growing history at every checkpoint.
    private final List<byte[]> completedScenarioJson = new ArrayList<>();
    private Context context;
    private int failures;
    private Bundle arguments;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        this.arguments = arguments;
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
            InterfaceAbortInstrumentation.configureRuntime(arguments);
            if (arguments != null && arguments.containsKey("checkjni"))
                initialization.put("checkJni", Boolean.parseBoolean(arguments.getString("checkjni")));
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
        boolean ordinaryOnly = arguments != null && "ordinary".equals(arguments.getString("scenario"));
        if (!ordinaryOnly && arguments != null && arguments.containsKey("scenario")) {
            String isolated = arguments.getString("scenario");
            if ("interface-default-body".equals(isolated)) runCopiedDefaultScenario();
            else if ("snapshot-benchmark".equals(isolated)) runSnapshotBenchmark();
            else if ("interface-abstract".equals(isolated)) runAbstractInterfaceScenarios();
            else if ("thread-subclass".equals(isolated)) runThreadSubclassScenario();
            else if ("narrowed-reference-method".equals(isolated)) {
                runNarrowedReferenceScenarios("method");
            }
            else if ("interface-removal".equals(isolated)) {
                runInterfaceRemovalBatchScenario();
                runInterfaceRemovalScenarios();
            }
            else if ("proxy-interface".equals(isolated)) runProxyScenarios();
            else if ("interface-drop-parent".equals(isolated)) runParentRemovalScenarios();
            else if ("static-narrow".equals(isolated)) runStaticNarrowingScenarios();
            else if ("field-retype".equals(isolated)) runRetypeScenarios();
            else if ("invoke-kind".equals(isolated)) runInvokeKindScenarios();
            else if ("method-private".equals(isolated)) runPrivateCallScenarios();
            else if ("tool".equals(isolated)) runToolScenarios();
            else if ("interface-shadow".equals(isolated)) runInterfaceShadowScenarios();
            else if ("invoke-interface-kind".equals(isolated)) runInterfaceInvokeKindScenarios();
            else if (isolated.startsWith("interface-abstract-unimplemented-"))
                runAbstractUnimplementedScenario(isolated.substring("interface-abstract-unimplemented-".length()));
            else throw new IllegalArgumentException("Unsupported isolated scenario");
            return;
        }
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
        // Widening to a boot superclass while adding an interface and retiring
        // the members that "add" introduced, in one transaction.
        runScenario("hierarchy", 0, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "hierarchy");
            Object widened = evolving;
            scenario.check("superclass", java.util.ArrayList.class.getName(),
                    Evolving.class.getSuperclass().getName());
            scenario.check("interfaces", Runnable.class.getName(), Evolving.class.getInterfaces()[0].getName());
            scenario.check("oldValue", 10, evolving.value);
            scenario.check("oldCompute", 16, evolving.compute());
            scenario.check("oldIsList", true, widened instanceof java.util.ArrayList);
            scenario.check("oldListSize", 0, ((java.util.ArrayList<?>) widened).size());
            ((Runnable) widened).run();
            scenario.check("oldRunnable", 11, evolving.value);
            scenario.expectError("retiredField", NoSuchFieldException.class,
                    () -> Evolving.class.getDeclaredField("added"));
            scenario.check("newCompute", 9, new Evolving(3).compute());
            scenario.check("staticCounter", 7, Evolving.counter);
        });

        ModifierSubject modifier = new ModifierSubject();
        ModifierPassive passive = new ModifierPassive();
        FieldSubject fields = new FieldSubject();
        String originalModifierSchema = memberSchema(ModifierSubject.class);
        String originalFieldSchema = memberSchema(FieldSubject.class);
        String originalFieldValues = fieldValues(fields);
        Object originalObject = fields.o;
        Object originalStaticObject = FieldSubject.so;
        ClassFinalSubject classFinal = new ClassFinalSubject();
        int originalClassFlags = ClassFinalSubject.class.getModifiers();
        String originalClassSchema = memberSchema(ClassFinalSubject.class);
        // Turning a class into an interface is always rejected, so the whole batch must stay.
        runScenario("atomic-modifier-public-class-interface", 70, scenario -> {
            scenario.check("modifierBefore", 42, modifier.throughPublic());
            scenario.check("passiveBefore", 42, passive.throughPublic());
            scenario.expectRejectionBy(ClassFinalSubject.class);
            scenario.redefine(new Class<?>[]{ModifierSubject.class, ClassFinalSubject.class},
                    "modifier-public", "class-interface");
            scenario.check("unchangedModifierMembers", originalModifierSchema, memberSchema(ModifierSubject.class));
            scenario.check("modifierThroughPublic", 42, modifier.throughPublic());
            scenario.check("passiveThroughPublic", 42, passive.throughPublic());
            scenario.check("modifierFinalizable", 29, modifier.finalizable());
            scenario.check("passiveFinalizable", 29, passive.finalizable());
            checkClassFinalUnchanged(scenario, classFinal, originalClassFlags, originalClassSchema);
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
        runScenario("class-final", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ClassFinalSubject.class}, "class-final");
            scenario.check("finalFlags", Modifier.PUBLIC | Modifier.FINAL, ClassFinalSubject.class.getModifiers());
            scenario.check("oldValue", 30, classFinal.value());
            scenario.check("newValue", 30, new ClassFinalSubject().value());
        });
        runScenario("class-nonfinal", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ClassFinalSubject.class}, "class-nonfinal");
            checkClassFinalUnchanged(scenario, classFinal, originalClassFlags, originalClassSchema);
        });
        runScenario("class-abstract-instances", 70, scenario -> {
            scenario.redefine(new Class<?>[]{ClassFinalSubject.class}, "class-abstract");
            checkClassFinalUnchanged(scenario, classFinal, originalClassFlags, originalClassSchema);
        });
        runScenario("class-abstract", 0, scenario -> {
            scenario.check("candidateBefore", 33, AbstractCandidate.marker());
            scenario.redefine(new Class<?>[]{AbstractCandidate.class}, "candidate-abstract");
            scenario.check("candidateFlags", Modifier.ABSTRACT, AbstractCandidate.class.getModifiers());
            Method marker = AbstractCandidate.class.getDeclaredMethod("marker");
            marker.setAccessible(true);
            scenario.check("candidateAfter", 34, marker.invoke(null));
        });
        runScenario("class-final-subclassed", 70, scenario -> {
            String before = memberSchema(ModifierSubject.class);
            int flags = ModifierSubject.class.getModifiers();
            scenario.redefine(new Class<?>[]{ModifierSubject.class}, "modifier-class-final");
            scenario.check("subclassedFlags", flags, ModifierSubject.class.getModifiers());
            scenario.check("unchangedMembers", before, memberSchema(ModifierSubject.class));
            scenario.check("passiveStillSubclass", true,
                    ModifierPassive.class.getSuperclass() == ModifierSubject.class);
        });
        runMethodScenarios(classFinal, originalClassFlags, originalClassSchema);
        runMethodKindChangeScenarios();
        if (!ordinaryOnly) {
            runNestedInterfaceScenarios();
            runInterfaceBatchScenarios();
            runInterfaceImplementationScenarios();
            runInterfaceOverlapScenarios();
            runMultiInterfaceScenarios();
            runPointerIdScenarios();
            runCopiedDefaultScenario();
            runInterfaceStressScenario();
            runInterfaceEvolutionScenarios();
            runAbstractInterfaceScenarios();
            runInterfaceStressAdditionScenario();
            runInterfaceRemovalBatchScenario();
            runProxyScenarios();
            runParentRemovalScenarios();
        }
        runInterfaceScenarios();
        runSuperclassScenarios();
        runPreloadScenarios();
        runToolScenarios();
        runFieldScenarios(fields, originalFieldSchema, originalFieldValues,
                originalObject, originalStaticObject);
        runRetypeScenarios();
        runInvokeKindScenarios();
        runThreadSubclassScenario();
        runInterfaceRemovalScenarios();
        if (!ordinaryOnly) runStaticNarrowingScenarios();
    }

    // A Proxy implementation of a redefined interface stays: kept methods still
    // reach the handler, now as methods of the new interface; a new abstract
    // method throws AbstractMethodError and a new default runs its body.
    private void runProxyScenarios() throws Exception {
        runProxyGroupScenarios();
        final int times = DISPATCH_CALLS;
        ProxyFixture.Api api = ProxyFixture.create();
        Class<?>[] batch = {ProxyFixture.Api.class};
        runScenario("proxy-interface", 0, scenario -> {
            scenario.check("getBefore", 5, ProxyFixture.get(api));
            scenario.check("twiceBefore", 77, api.twice());
            scenario.check("sumBefore", 5 * times, ProxyFixture.sum(api, times));
            scenario.redefine(batch, "proxy-api-add");
            // Young objects, then a collection, before the first proxy call.
            byte[][] garbage = new byte[4096][];
            for (int i = 0; i < 64 * garbage.length; i++) garbage[i % garbage.length] = new byte[64];
            Runtime.getRuntime().gc();
            scenario.check("interfaceIsNew", true, api.getClass().getInterfaces()[0] == ProxyFixture.Api.class);
            scenario.check("get", 5, ProxyFixture.get(api));
            scenario.check("handlerSeesNewMethod", true, ProxyFixture.currentInterface);
            scenario.check("twice", 77, api.twice());
            scenario.check("sum", 5 * times, ProxyFixture.sum(api, times));
            scenario.expectError("removed", NoSuchMethodError.class, () -> ProxyFixture.removed(api));
            scenario.expectError("added", AbstractMethodError.class,
                    () -> ProxyFixture.Api.class.getMethod("added").invoke(api));
            scenario.check("extra", 40, ProxyFixture.Api.class.getMethod("extra").invoke(api));
            ProxyFixture.Api fresh = ProxyFixture.create();
            scenario.check("freshSameClass", true, fresh.getClass() == api.getClass());
            scenario.check("freshGet", 5, ProxyFixture.get(fresh));
            Runtime.getRuntime().gc();
            scenario.check("afterGc", 5, ProxyFixture.get(api));
        });
        runScenario("proxy-interface-restore", 0, scenario -> {
            scenario.redefine(batch, "proxy-api-original");
            scenario.check("get", 5, ProxyFixture.get(api));
            scenario.check("handlerSeesNewMethod", true, ProxyFixture.currentInterface);
            scenario.check("removed", 6, ProxyFixture.removed(api));
            scenario.check("twice", 77, api.twice());
            scenario.check("sum", 5 * times, ProxyFixture.sum(api, times));
        });
        // As on JBR, the proxy class stays and gains the new parent: kept methods
        // still reach the handler, the parent's default runs its body and its
        // abstract method throws AbstractMethodError. Dropping it again still rejects, unlike JBR.
        runScenario("proxy-interface-parent", 0, scenario -> {
            scenario.check("parentBefore", false, ProxyFixture.isParent(api));
            scenario.redefine(batch, "proxy-parent-add");
            Runtime.getRuntime().gc();
            scenario.check("isParent", true, ProxyFixture.isParent(api));
            scenario.check("directInterfaces", 1, api.getClass().getInterfaces().length);
            scenario.check("get", 5, ProxyFixture.get(api));
            scenario.check("handlerSeesNewMethod", true, ProxyFixture.currentInterface);
            scenario.check("twice", 77, api.twice());
            scenario.check("parentDefault", 41, ProxyFixture.parentDefault(api));
            scenario.check("parentReflection", 41, ProxyFixture.Parent.class.getMethod("parentDefault").invoke(api));
            scenario.expectError("parentAbstract", AbstractMethodError.class,
                    () -> ProxyFixture.Parent.class.getMethod("parentAbstract").invoke(api));
            int sum = 0;
            for (int i = 0; i < times; i++) sum += ProxyFixture.parentDefault(api);
            scenario.check("parentSum", 41 * times, sum);
            ProxyFixture.Api fresh = ProxyFixture.create();
            scenario.check("freshSameClass", true, fresh.getClass() == api.getClass());
            scenario.check("freshIsParent", true, ProxyFixture.isParent(fresh));
            Runtime.getRuntime().gc();
            scenario.check("afterGc", 41, ProxyFixture.parentDefault(api));
        });
        runScenario("proxy-interface-parent-drop", 66, scenario -> {
            scenario.redefine(batch, "proxy-api-original");
            scenario.check("isParent", true, ProxyFixture.isParent(api));
            scenario.check("get", 5, ProxyFixture.get(api));
            scenario.check("parentDefault", 41, ProxyFixture.parentDefault(api));
        });
    }

    private void runStaticNarrowingScenarios() throws Exception {
        StaticNarrowingFixture.Shared shared =
                new StaticNarrowingFixture.Shared();
        StaticNarrowingFixture.Compatible compatible =
                new StaticNarrowingFixture.Compatible();
        Class<?> owner = StaticNarrowingFixture.Shared.class;
        Class<?> holder = StaticNarrowingFixture.Holder.class;
        Class<?>[] batch = {StaticNarrowingFixture.Child.class,
                StaticNarrowingFixture.Side.class, owner, holder, StaticNarrowingFixture.Compatible.class};
        String[] array = {"kept"};
        for (String name : new String[]{"array", "objectArray", "cloneableArray", "serializableArray"}) {
            owner.getField(name).set(null, array);
            holder.getField(name).set(null, array);
        }
        StaticNarrowingFixture.Shared[] narrowedArray = {shared};
        StaticNarrowingFixture.Shared[][] narrowedMatrix = {narrowedArray};
        for (Class<?> storage : new Class<?>[]{owner, holder}) {
            storage.getField("covariant").set(null, narrowedArray);
            storage.getField("matrix").set(null, narrowedMatrix);
            storage.getField("retypedArray").set(null, narrowedArray);
            storage.getField("unsubmitted").set(null, new StaticNarrowingFixture.Unsubmitted());
        }
        owner.getField("self").set(null, shared);
        owner.getField("compatible").set(null, compatible);
        owner.getField("retyped").set(null, shared);
        holder.getField("self").set(null, shared);
        holder.getField("compatible").set(null, compatible);
        runScenario("static-narrow-abort", 110, scenario -> {
            scenario.check("armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
            scenario.redefine(batch, "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove");
            scenario.check("self", true, owner.getField("self").get(null) == shared);
            scenario.check("holderSelf", true, holder.getField("self").get(null) == shared);
            scenario.check("retyped", true, owner.getField("retyped").get(null) == shared);
            scenario.check("oldType", Object.class.getName(), owner.getField("retyped").getType().getName());
        });
        runScenario("static-narrow-remove", 0, scenario -> {
            scenario.redefine(batch, "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove");
            for (String name : new String[]{"array", "objectArray", "cloneableArray", "serializableArray"}) {
                scenario.check("ownerArray-" + name, true, owner.getField(name).get(null) == array);
                scenario.check("holderArray-" + name, true, holder.getField(name).get(null) == array);
            }
            for (Class<?> storage : new Class<?>[]{owner, holder}) {
                String label = storage == owner ? "owner" : "holder";
                scenario.check(label + "CovariantKept", true, storage.getField("covariant").get(null) == narrowedArray);
                scenario.check(label + "MatrixKept", true, storage.getField("matrix").get(null) == narrowedMatrix);
                scenario.check(label + "ArrayRetypeNull", null, storage.getField("retypedArray").get(null));
                scenario.check(label + "UnsubmittedNull", null, storage.getField("unsubmitted").get(null));
            }
            scenario.check("bodyOnlyCompatible", 22, compatible.value());
            scenario.check("selfCleared", null, owner.getField("self").get(null));
            scenario.check("compatibleKept", true, owner.getField("compatible").get(null) == compatible);
            scenario.check("retypedCleared", null, owner.getField("retyped").get(null));
            scenario.check("holderSelfCleared", null, holder.getField("self").get(null));
            scenario.check("holderCompatibleKept", true, holder.getField("compatible").get(null) == compatible);
            scenario.check("newType", StaticNarrowingFixture.Base.class.getName(),
                    owner.getField("retyped").getType().getName());
            Runtime.getRuntime().gc();
            scenario.check("marker", 7, shared.marker);
            scenario.check("afterGc", true, owner.getField("compatible").get(null) == compatible);
        });
        runScenario("static-narrow-restore", 0, scenario -> {
            scenario.redefine(batch, "static-narrow-original", "static-narrow-original", "static-narrow-original", "static-narrow-original", "static-narrow-original");
            scenario.check("selfStillNull", null, owner.getField("self").get(null));
            scenario.check("compatibleKept", true, owner.getField("compatible").get(null) == compatible);
        });
        owner.getField("self").set(null, shared);
        owner.getField("retyped").set(null, shared);
        runScenario("static-narrow-hook", 0, scenario -> {
            scenario.check("hookInstalled", 0, StandardJvmtiClient.setClassFileReplacement(
                    owner.getName().replace('.', '/'), readAsset("static-narrow-hook/"
                            + owner.getName().replace('.', '/') + ".dex")));
            try {
                scenario.redefine(batch, "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove");
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            scenario.expectError("hookDeletedField", NoSuchFieldException.class, () -> owner.getField("objectArray"));
            scenario.check("hookRestoredType", Object.class.getName(), owner.getField("retyped").getType().getName());
            scenario.check("hookRestoredValue", true, owner.getField("retyped").get(null) == shared);
            scenario.check("hookSelfCleared", null, owner.getField("self").get(null));
        });
        runStaticNarrowingLoaders();
    }

    private void runStaticNarrowingLoaders() throws Exception {
        String prefix = "com.github.ytlog.artdcevm.fixture.StaticNarrowingFixture";
        String[] suffixes = {"", "$Base", "$Child", "$Side", "$Shared", "$Compatible", "$CompatibleChild", "$Unsubmitted", "$Holder"};
        java.nio.ByteBuffer[] dexes = new java.nio.ByteBuffer[suffixes.length];
        for (int i = 0; i < suffixes.length; ++i)
            dexes[i] = java.nio.ByteBuffer.wrap(readAsset("static-narrow-original/"
                    + (prefix + suffixes[i]).replace('.', '/') + ".dex"));
        ClassLoader[] loaders = new ClassLoader[2];
        Class<?>[] batch = new Class<?>[10];
        Class<?>[] owners = new Class<?>[2];
        Object[] values = new Object[2];
        Object[] selves = new Object[2];
        for (int i = 0; i < 2; ++i) {
            loaders[i] = new dalvik.system.InMemoryDexClassLoader(dexes, context.getClassLoader().getParent());
            String[] targets = i == 0 ? new String[]{"$Child", "$Side", "$Shared", "$Holder", "$Compatible"}
                    : new String[]{"$Compatible", "$Holder", "$Shared", "$Side", "$Child"};
            for (int j = 0; j < targets.length; ++j)
                batch[i * 5 + j] = Class.forName(prefix + targets[j], true, loaders[i]);
            owners[i] = Class.forName(prefix + "$Shared", true, loaders[i]);
            selves[i] = owners[i].getConstructor().newInstance();
            values[i] = Class.forName(prefix + (i == 0 ? "$Compatible" : "$CompatibleChild"), true, loaders[i]).getConstructor().newInstance();
            owners[i].getField("self").set(null, selves[i]);
            owners[i].getField("compatible").set(null, values[i]);
            owners[i].getField("retyped").set(null, values[i]);
        }
        runScenario("static-narrow-loaders", 0, scenario -> {
            scenario.check("distinctClasses", false, owners[0] == owners[1]);
            scenario.redefine(batch, "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove", "static-narrow-remove",
                    "static-narrow-struct", "static-narrow-struct", "static-narrow-struct", "static-narrow-struct", "static-narrow-struct");
            Runtime.getRuntime().gc();
            for (int i = 0; i < owners.length; ++i) {
                scenario.check("selfCleared" + i, null, owners[i].getField("self").get(null));
                scenario.check("compatibleKept" + i, true, owners[i].getField("compatible").get(null) == values[i]);
                scenario.check("retypedKept" + i, true, owners[i].getField("retyped").get(null) == values[i]);
                scenario.check("ownLoader" + i, true, owners[i].getField("retyped").getType().getClassLoader() == loaders[i]);
            }
        });
    }

    private void runProxyGroupScenarios() throws Exception {
        Object proxy = ProxyGroupFixture.create();
        Class<?> base = ProxyGroupFixture.Base.class;
        Class<?> child = ProxyGroupFixture.Child.class;
        Class<?> side = ProxyGroupFixture.Side.class;
        Class<?>[] forward = {base, child, side};
        Class<?>[] reverse = {side, child, base};
        runScenario("proxy-interface-group-abort", 110, scenario -> {
            scenario.check("pureProxy", false, StandardJvmtiClient.isLoaded(
                    "Lcom/github/ytlog/artdcevm/fixture/ProxyGroupFixture$Shared;"));
            scenario.check("armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
            scenario.redefine(forward, "proxy-group-add", "proxy-group-add", "proxy-group-add");
            scenario.check("base", 21, base.getMethod("base").invoke(proxy));
            scenario.check("child", 22, child.getMethod("child").invoke(proxy));
            scenario.check("side", 23, side.getMethod("side").invoke(proxy));
            scenario.expectError("unpublishedBase", NoSuchMethodException.class, () -> base.getMethod("extraBase"));
            scenario.expectError("unpublishedChild", NoSuchMethodException.class, () -> child.getMethod("extraChild"));
            scenario.expectError("unpublishedSide", NoSuchMethodException.class, () -> side.getMethod("extraSide"));
        });
        runScenario("proxy-interface-group-pure", 0, scenario -> {
            scenario.redefine(forward, "proxy-group-add", "proxy-group-add", "proxy-group-add");
            checkProxyGroup(scenario, proxy, true);
        });
        // Loading the common ordinary implementation joins the parent/child
        // and unrelated interface targets into one owner with multiple entries.
        Object shared = Class.forName("com.github.ytlog.artdcevm.fixture.ProxyGroupFixture$Shared")
                .getConstructor().newInstance();
        runScenario("proxy-interface-group-restore", 0, scenario -> {
            scenario.redefine(reverse, "proxy-group-original", "proxy-group-original", "proxy-group-original");
            checkProxyGroup(scenario, proxy, false);
            scenario.check("shared", 7, shared.getClass().getField("marker").getInt(shared));
        });
        runScenario("proxy-interface-group-shared", 0, scenario -> {
            scenario.redefine(reverse, "proxy-group-add", "proxy-group-add", "proxy-group-add");
            Runtime.getRuntime().gc();
            checkProxyGroup(scenario, proxy, true);
            scenario.check("shared", 7, shared.getClass().getField("marker").getInt(shared));
            scenario.check("sharedBase", 31, ProxyGroupFixture.Base.class
                    .getMethod("extraBase").invoke(shared));
        });
        runScenario("proxy-interface-group-final", 0, scenario -> {
            scenario.redefine(forward, "proxy-group-original", "proxy-group-original", "proxy-group-original");
            checkProxyGroup(scenario, proxy, false);
        });
    }

    private void checkProxyGroup(Scenario scenario, Object proxy, boolean added) throws Exception {
        Class<?> base = ProxyGroupFixture.Base.class;
        Class<?> child = ProxyGroupFixture.Child.class;
        Class<?> side = ProxyGroupFixture.Side.class;
        scenario.check("base", 21, base.getMethod("base").invoke(proxy));
        scenario.check("child", 22, child.getMethod("child").invoke(proxy));
        scenario.check("side", 23, side.getMethod("side").invoke(proxy));
        if (added) {
            scenario.check("extraBase", 31, base.getMethod("extraBase").invoke(proxy));
            scenario.check("extraChild", 32, child.getMethod("extraChild").invoke(proxy));
            scenario.check("extraSide", 33, side.getMethod("extraSide").invoke(proxy));
        }
    }

    // Child drops its parent Base: Impl, which implements Child, stops being a
    // Base, and calls verified code still makes through Base throw
    // IncompatibleClassChangeError, interpreted and compiled.
    private void runParentRemovalScenarios() throws Exception {
        final int times = DISPATCH_CALLS;
        ParentRemovalFixture.Impl impl = new ParentRemovalFixture.Impl();
        Class<?>[] batch = {ParentRemovalFixture.Child.class};
        runScenario("interface-drop-parent", 0, scenario -> {
            scenario.check("warmedUp", 0, ParentRemovalFixture.failures(impl, times));
            scenario.check("defaultBefore", 7, ParentRemovalFixture.baseDefault(impl));
            scenario.redefine(batch, "parent-remove");
            scenario.check("parents", 0, ParentRemovalFixture.Child.class.getInterfaces().length);
            scenario.check("isBase", false, ParentRemovalFixture.Base.class.isInstance(impl));
            scenario.check("isChild", true, ParentRemovalFixture.Child.class.isInstance(impl));
            scenario.check("child", 12, ParentRemovalFixture.child(impl));
            scenario.check("side", 22, ParentRemovalFixture.side(impl));
            scenario.expectError("viaChild", IncompatibleClassChangeError.class,
                    () -> ParentRemovalFixture.viaChild(impl));
            scenario.check("compiledFailures", times, ParentRemovalFixture.failures(impl, times));
            scenario.expectError("copiedDefault", NoSuchMethodError.class,
                    () -> ParentRemovalFixture.baseDefault(impl));
            Runtime.getRuntime().gc();
            scenario.check("afterGc", 12, ParentRemovalFixture.child(impl));
        });
        runScenario("interface-drop-parent-restore", 0, scenario -> {
            scenario.redefine(batch, "parent-remove-original");
            scenario.check("isBase", true, ParentRemovalFixture.Base.class.isInstance(impl));
            scenario.check("viaChild", 11, ParentRemovalFixture.viaChild(impl));
            scenario.check("compiledFailures", 0, ParentRemovalFixture.failures(impl, times));
            scenario.check("copiedDefault", 7, ParentRemovalFixture.baseDefault(impl));
            scenario.check("side", 22, ParentRemovalFixture.side(impl));
        });
    }

    // The interface group's owner carries the whole replacement set; narrowing
    // one submitted implementation seals that set before ART commits it.
    private void runInterfaceRemovalBatchScenario() throws Exception {
        InterfaceRemovalFixture.Subject subject = new InterfaceRemovalFixture.Subject();
        runScenario("interface-removal-batched", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceRemovalFixture.Subject.class, InterfaceRemovalFixture.Kept.class},
                    "interface-remove", "interface-remove");
            scenario.check("removed", false, InterfaceRemovalFixture.Removed.class.isInstance(subject));
            scenario.check("kept", 22, InterfaceRemovalFixture.kept(subject));
            scenario.check("failures", DISPATCH_CALLS, InterfaceRemovalFixture.failures(subject, DISPATCH_CALLS));
        });
        runScenario("interface-removal-batched-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceRemovalFixture.Subject.class, InterfaceRemovalFixture.Kept.class},
                    "interface-remove-original", "interface-remove-original");
            scenario.check("restored", true, InterfaceRemovalFixture.Removed.class.isInstance(subject));
            scenario.check("viaRemoved", 11, InterfaceRemovalFixture.viaRemoved(subject));
        });
    }

    // Subject drops Removed and Task drops Runnable. Calls that verified code
    // still makes through the removed interface throw
    // IncompatibleClassChangeError on every dispatch path: interpreted,
    // compiled once hot, and from boot code (Thread.run).
    private void runInterfaceRemovalScenarios() throws Exception {
        final int times = DISPATCH_CALLS;
        InterfaceRemovalFixture.Subject subject = new InterfaceRemovalFixture.Subject();
        InterfaceRemovalFixture.Child child = new InterfaceRemovalFixture.Child();
        InterfaceRemovalFixture.Task task = new InterfaceRemovalFixture.Task();
        Thread thread = new Thread(task);
        InterfaceRemovalFixture.held = subject;
        String late = "Lcom/github/ytlog/artdcevm/fixture/InterfaceRemovalFixture$LateChild;";
        Class<?>[] batch = {InterfaceRemovalFixture.Subject.class, InterfaceRemovalFixture.Task.class};
        Object[] lateChild = new Object[1];
        runScenario("interface-removal", 0, scenario -> {
            scenario.check("warmedUp", 0, InterfaceRemovalFixture.failures(subject, times));
            thread.run();
            scenario.check("runBefore", 1, task.runs);
            scenario.check("lateLoadedBefore", false, StandardJvmtiClient.isLoaded(late));
            InterfaceRemovalFixture.Subject.self = subject;
            InterfaceRemovalFixture.Subject.keptSelf = subject;
            scenario.redefine(batch, "interface-remove", "interface-remove");
            // As on JBR: the target's own static typed with the removed
            // interface is cleared; one typed with a kept interface is not.
            scenario.check("staticCleared", true,
                    InterfaceRemovalFixture.Subject.class.getField("self").get(null) == null);
            scenario.check("staticKept", true, InterfaceRemovalFixture.Subject.keptSelf == subject);
            scenario.check("removed", false, InterfaceRemovalFixture.Removed.class.isInstance(subject));
            scenario.check("interfaces", "[" + InterfaceRemovalFixture.Kept.class + "]",
                    java.util.Arrays.toString(InterfaceRemovalFixture.Subject.class.getInterfaces()));
            scenario.check("marker", 5, subject.marker);
            scenario.check("kept", 22, InterfaceRemovalFixture.kept(subject));
            scenario.expectError("viaRemoved", IncompatibleClassChangeError.class,
                    () -> InterfaceRemovalFixture.viaRemoved(subject));
            scenario.check("compiledFailures", times, InterfaceRemovalFixture.failures(subject, times));
            scenario.expectError("held", IncompatibleClassChangeError.class,
                    () -> InterfaceRemovalFixture.call(InterfaceRemovalFixture.held));
            scenario.expectError("copiedDefault", NoSuchMethodError.class,
                    () -> InterfaceRemovalFixture.callDefault(subject));
            scenario.check("childRemoved", false, InterfaceRemovalFixture.Removed.class.isInstance(child));
            scenario.check("childKept", 22, InterfaceRemovalFixture.kept(child));
            scenario.check("childFailures", times, InterfaceRemovalFixture.failures(child, times));
            thread.run();
            scenario.check("bootDispatch", true, true);
            scenario.check("runAfter", 1, task.runs);
            ((InterfaceRemovalFixture.Spinner) (Object) task).spin9();
            scenario.check("spins", 1, task.spins);
            lateChild[0] = Class.forName("com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture$LateChild")
                    .getConstructor().newInstance();
            InterfaceRemovalFixture.Subject lateSubject = (InterfaceRemovalFixture.Subject) lateChild[0];
            scenario.check("lateKept", 22, InterfaceRemovalFixture.kept(lateSubject));
            scenario.check("lateOwn", 33, ((InterfaceRemovalFixture.Late) lateChild[0]).late22());
            scenario.check("lateFailures", times, InterfaceRemovalFixture.failures(lateSubject, times));
            InterfaceRemovalFixture.Subject override = (InterfaceRemovalFixture.Subject) Class.forName(
                    "com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture$LateOverride")
                    .getConstructor().newInstance();
            scenario.check("overrideKept", 44, InterfaceRemovalFixture.kept(override));
            scenario.check("parentCacheKept", 22, InterfaceRemovalFixture.kept(lateSubject));
            scenario.check("overrideFailures", times, InterfaceRemovalFixture.failures(override, times));
            Runtime.getRuntime().gc();
            scenario.check("afterGc", 5, subject.marker);
        });
        runScenario("interface-removal-repeat", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceRemovalFixture.Subject.class}, "interface-remove-again");
            scenario.check("added", 0L, InterfaceRemovalFixture.Subject.class.getField("added").getLong(subject));
            scenario.check("kept", 22, InterfaceRemovalFixture.kept(subject));
            scenario.check("failures", times, InterfaceRemovalFixture.failures(subject, times));
            scenario.check("childFailures", times, InterfaceRemovalFixture.failures(child, times));
        });
        runScenario("interface-removal-group", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceRemovalFixture.Kept.class}, "interface-remove");
            scenario.check("kept", 22, InterfaceRemovalFixture.kept(subject));
            scenario.check("failures", times, InterfaceRemovalFixture.failures(subject, times));
            scenario.check("lateFailures", times, InterfaceRemovalFixture.failures(
                    (InterfaceRemovalFixture.Subject) lateChild[0], times));
        });
        runScenario("interface-removal-restore", 0, scenario -> {
            scenario.redefine(batch, "interface-remove-original", "interface-remove-original");
            InterfaceRemovalFixture.Subject lateSubject = (InterfaceRemovalFixture.Subject) lateChild[0];
            scenario.check("restored", true, InterfaceRemovalFixture.Removed.class.isInstance(subject));
            scenario.check("viaRemoved", 11, InterfaceRemovalFixture.viaRemoved(subject));
            scenario.check("compiledFailures", 0, InterfaceRemovalFixture.failures(subject, times));
            scenario.check("held", 11, InterfaceRemovalFixture.call(InterfaceRemovalFixture.held));
            scenario.check("copiedDefault", 7, InterfaceRemovalFixture.callDefault(subject));
            scenario.check("lateFailures", 0, InterfaceRemovalFixture.failures(lateSubject, times));
            scenario.check("lateOwn", 33, ((InterfaceRemovalFixture.Late) lateChild[0]).late22());
            thread.run();
            scenario.check("runRestored", 1, task.runs);
            new Thread(task).run();
            scenario.check("freshRunRestored", 2, task.runs);
        });
        runAbstractRemovalScenarios();
        runNarrowedReferenceScenarios("remove", "body", "method", "structural", "static", "interface");
    }

    private void runAbstractRemovalScenarios() throws Exception {
        final int times = DISPATCH_CALLS;
        AbstractRemovalFixture.Existing existing = new AbstractRemovalFixture.Existing();
        AbstractRemovalFixture.Task existingTask = new AbstractRemovalFixture.ExistingTask();
        Thread before = new Thread(existingTask);
        before.run();
        Class<?>[] batch = {AbstractRemovalFixture.Subject.class, AbstractRemovalFixture.Task.class};
        AbstractRemovalFixture.Subject[] late = new AbstractRemovalFixture.Subject[1];
        runScenario("abstract-removal-abort", 110, scenario -> {
            long visits = InterfaceAbortInstrumentation.referenceCleanupVisits();
            scenario.check("armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
            scenario.expectRejectionBy(AbstractRemovalFixture.Subject.class);
            scenario.redefine(batch, "abstract-remove", "abstract-remove");
            scenario.check("stillImplements", true, AbstractRemovalFixture.Removed.class.isInstance(existing));
            scenario.check("failures", 0, AbstractRemovalFixture.failures(existing, times));
            scenario.check("noCleanup", true, visits == InterfaceAbortInstrumentation.referenceCleanupVisits());
            before.run();
            scenario.check("threadStillHeld", 2, existingTask.runs);
        });
        runScenario("abstract-removal", 0, scenario -> {
            scenario.check("lateLoadedBefore", false, StandardJvmtiClient.isLoaded(
                    "Lcom/github/ytlog/artdcevm/fixture/AbstractRemovalFixture$LateChild;"));
            scenario.redefine(batch, "abstract-remove", "abstract-remove");
            scenario.check("removed", false, AbstractRemovalFixture.Removed.class.isInstance(existing));
            scenario.check("existingFailures", times, AbstractRemovalFixture.failures(existing, times));
            late[0] = (AbstractRemovalFixture.Subject) Class.forName(
                    "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateChild")
                    .getConstructor().newInstance();
            scenario.check("lateKept", 22, late[0].kept31());
            scenario.check("lateOwn", 33, ((AbstractRemovalFixture.Late) late[0]).late22());
            scenario.check("lateFailures", times, AbstractRemovalFixture.failures(late[0], times));
            AbstractRemovalFixture.Subject direct = (AbstractRemovalFixture.Subject) Class.forName(
                    "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateDirect")
                    .getConstructor().newInstance();
            scenario.check("directKept", 55, AbstractRemovalFixture.kept(direct));
            scenario.check("directFailures", times, AbstractRemovalFixture.failures(direct, times));
            scenario.check("existingKept", 22, AbstractRemovalFixture.kept(existing));
            AbstractRemovalFixture.Task lateTask = (AbstractRemovalFixture.Task) Class.forName(
                    "com.github.ytlog.artdcevm.fixture.AbstractRemovalFixture$LateTask")
                    .getConstructor().newInstance();
            Thread after = AbstractRemovalFixture.thread(lateTask);
            int bootFailures = 0;
            for (int i = 0; i < times; ++i) {
                try { after.run(); }
                catch (IncompatibleClassChangeError expected) { bootFailures++; }
            }
            scenario.check("lateBootFailures", times, bootFailures);
            scenario.check("lateSpins", 0, lateTask.spins);
            before.run();
            scenario.check("existingBootDispatch", true, true);
            scenario.check("existingRuns", 2, existingTask.runs);
        });
        runScenario("abstract-removal-repeat", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractRemovalFixture.Subject.class}, "abstract-remove-again");
            scenario.check("added", 0L, AbstractRemovalFixture.Subject.class.getField("added").getLong(existing));
            scenario.check("existingFailures", times, AbstractRemovalFixture.failures(existing, times));
            scenario.check("lateFailures", times, AbstractRemovalFixture.failures(late[0], times));
            Runtime.getRuntime().gc();
            scenario.check("marker", 5, late[0].marker);
        });
        runScenario("abstract-removal-group", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractRemovalFixture.Kept.class}, "abstract-remove");
            scenario.check("kept", 22, late[0].kept31());
            scenario.check("lateFailures", times, AbstractRemovalFixture.failures(late[0], times));
        });
        runScenario("abstract-removal-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractRemovalFixture.Subject.class}, "abstract-remove-original");
            scenario.check("existingFailures", 0, AbstractRemovalFixture.failures(existing, times));
            scenario.check("lateFailures", 0, AbstractRemovalFixture.failures(late[0], times));
        });
    }

    private void runNarrowedReferenceScenarios(String... modes) throws Exception {
        for (String mode : modes) {
            NarrowedReferenceFixture.Subject value = new NarrowedReferenceFixture.Subject();
            NarrowedReferenceFixture.Direct direct = new NarrowedReferenceFixture.Direct();
            NarrowedReferenceFixture.Indirect indirect = new NarrowedReferenceFixture.Indirect();
            NarrowedReferenceFixture.Holder holder = new NarrowedReferenceFixture.HolderChild();
            value.self = value; value.direct = direct; value.indirect = indirect; value.retained = value;
            holder.removed = value; holder.direct = direct; holder.inherited = new NarrowedReferenceFixture.Inherited();
            holder.indirect = indirect; holder.kept = value; holder.object = value;
            runScenario("narrowed-reference-" + mode, 0, scenario -> {
                long visits = InterfaceAbortInstrumentation.referenceCleanupVisits();
                long clears = InterfaceAbortInstrumentation.referenceCleanupClears();
                if ("remove".equals(mode))
                    scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class}, "narrowed-reference-remove");
                else if ("interface".equals(mode))
                    scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class, NarrowedReferenceFixture.Kept.class},
                            "narrowed-reference-interface", "narrowed-reference-interface");
                else scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class, NarrowedReferenceFixture.Holder.class},
                        "narrowed-reference-" + mode, "narrowed-reference-" + mode);
                scenario.check("self", true, "interface".equals(mode) ? value.self == null : value.self == value);
                scenario.check("selfDirect", true, value.direct == direct);
                scenario.check("selfIndirect", true, "interface".equals(mode) ? value.indirect == null : value.indirect == indirect);
                scenario.check("selfKept", true, value.retained == value);
                scenario.check("holder", !("body".equals(mode) || "method".equals(mode)), holder.removed == null);
                scenario.check("holderDirect", true, holder.direct == direct);
                scenario.check("holderInherited", true, holder.inherited != null);
                scenario.check("holderIndirect", !("body".equals(mode) || "method".equals(mode)), holder.indirect == null);
                scenario.check("holderKept", true, holder.kept == value);
                scenario.check("holderObject", true, holder.object == value);
                if ("method".equals(mode)) {
                    scenario.check("addedMethod", 7, NarrowedReferenceFixture.Holder.class.getMethod("addedMethod").invoke(holder));
                    scenario.check("staticMarker", 9, NarrowedReferenceFixture.Holder.staticMarker);
                }
                scenario.result.put("cleanupVisits", InterfaceAbortInstrumentation.referenceCleanupVisits() - visits);
                scenario.result.put("cleanupClears", InterfaceAbortInstrumentation.referenceCleanupClears() - clears);
                scenario.check("visited", !("body".equals(mode) || "method".equals(mode)),
                        InterfaceAbortInstrumentation.referenceCleanupVisits() > visits);
                scenario.check("cleared", !("body".equals(mode) || "method".equals(mode)),
                        InterfaceAbortInstrumentation.referenceCleanupClears() > clears);
                Runtime.getRuntime().gc();
                scenario.check("afterGc", true, holder.object == value);
            });
            runScenario("narrowed-reference-" + mode + "-restore", 0, scenario -> {
                scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class, NarrowedReferenceFixture.Holder.class, NarrowedReferenceFixture.Kept.class},
                        "narrowed-reference-original", "narrowed-reference-original", "narrowed-reference-original");
                scenario.check("restored", true, NarrowedReferenceFixture.Removed.class.isInstance(value));
            });
        }
        if (modes.length == 1) return;
        NarrowedReferenceFixture.Subject subject = new NarrowedReferenceFixture.Subject();
        runScenario("narrowed-reference-late-holder", 0, scenario -> {
            scenario.check("notLoaded", false, StandardJvmtiClient.isLoaded(
                    "Lcom/github/ytlog/artdcevm/fixture/NarrowedReferenceFixture$LateHolder;"));
            long retries = DcevmRuntime.capabilities().getLong("subclassLoadRetries");
            scenario.check("hook", 0, StandardJvmtiClient.setClassFileLoads(
                    NarrowedReferenceFixture.Subject.class.getName().replace('.', '/'),
                    NarrowedReferenceFixture.Subject.class.getClassLoader(),
                    new String[]{"com.github.ytlog.artdcevm.fixture.NarrowedReferenceFixture$LateHolder"}));
            try { scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class}, "narrowed-reference-remove"); }
            finally { StandardJvmtiClient.setClassFileLoads(null, null, null); }
            scenario.check("retried", 1L, DcevmRuntime.capabilities().getLong("subclassLoadRetries") - retries);
            scenario.check("removed", false, NarrowedReferenceFixture.Removed.class.isInstance(subject));
        });
        runScenario("narrowed-reference-late-holder-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class}, "narrowed-reference-original");
            scenario.check("restored", true, NarrowedReferenceFixture.Removed.class.isInstance(subject));
        });
        NarrowedReferenceFixture.Holder holder = new NarrowedReferenceFixture.Holder();
        holder.removed = subject; holder.kept = subject; holder.object = subject;
        runScenario("narrowed-reference-hook", 0, scenario -> {
            long retries = DcevmRuntime.capabilities().getLong("subclassLoadRetries");
            long replacements = StandardJvmtiClient.classFileReplacements();
            scenario.check("hook", 0, StandardJvmtiClient.setClassFileReplacement(
                    NarrowedReferenceFixture.Subject.class.getName().replace('.', '/'),
                    readAsset("narrowed-reference-remove/"
                            + NarrowedReferenceFixture.Subject.class.getName().replace('.', '/') + ".dex")));
            try {
                // The submitted bytes retain the interface; only the hook's
                // output reveals that the Native phase needs a reference snapshot.
                scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class}, "narrowed-reference-original");
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            scenario.check("retried", 1L, DcevmRuntime.capabilities().getLong("subclassLoadRetries") - retries);
            scenario.check("hookReplacements", 2L, StandardJvmtiClient.classFileReplacements() - replacements);
            scenario.check("removed", false, NarrowedReferenceFixture.Removed.class.isInstance(subject));
            scenario.check("holderCleared", true, holder.removed == null);
            scenario.check("kept", true, holder.kept == subject);
            Runtime.getRuntime().gc();
            scenario.check("afterGc", true, holder.object == subject);
        });
        runScenario("narrowed-reference-hook-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NarrowedReferenceFixture.Subject.class}, "narrowed-reference-original");
            scenario.check("restored", true, NarrowedReferenceFixture.Removed.class.isInstance(subject));
            scenario.check("clearedStillNull", true, holder.removed == null);
        });
    }

    // A reference field whose declared type changes keeps an old value that
    // the new type accepts (JBR's DcevmSharedGC) and starts from null otherwise,
    // in the target's instances, its loaded subclass's instances and statics.
    private void runRetypeScenarios() throws Exception {
        RetypeFixture.Subject subject = new RetypeFixture.Subject();
        RetypeFixture.Child child = new RetypeFixture.Child();
        String text = subject.text;
        Object number = subject.number, task = subject.task, childTask = child.task, inherited = subject.inherited;
        Object proxy = java.lang.reflect.Proxy.newProxyInstance(RetypeFixture.class.getClassLoader(),
                new Class<?>[]{Runnable.class}, (receiver, method, args) -> null);
        Object indirectProxy = java.lang.reflect.Proxy.newProxyInstance(RetypeFixture.class.getClassLoader(),
                new Class<?>[]{RetypeFixture.SubTask.class}, (receiver, method, args) -> null);
        subject.proxy = child.proxy = proxy;
        subject.indirectProxy = child.indirectProxy = indirectProxy;
        subject.proxyBase = child.proxyBase = proxy;
        Class<?> type = RetypeFixture.Subject.class;
        runScenario("field-retype", 0, scenario -> {
            scenario.redefine(new Class<?>[]{type}, "field-retype");
            scenario.check("textType", "java.lang.CharSequence", type.getField("text").getType().getName());
            scenario.check("textKept", true, type.getField("text").get(subject) == text);
            scenario.check("numberKept", true, type.getField("number").get(subject) == number);
            scenario.check("mismatchCleared", true, type.getField("mismatch").get(subject) == null);
            scenario.check("interfaceKept", true, type.getField("task").get(subject) == task);
            // JBR's FieldCopier names the type in the class chain or among
            // directly declared interfaces; a superinterface does not count.
            scenario.check("subinterfaceCleared", true, type.getField("deep").get(subject) == null);
            scenario.check("inheritedKept", true, type.getField("inherited").get(subject) == inherited);
            scenario.check("proxyKept", true, type.getField("proxy").get(subject) == proxy);
            scenario.check("indirectProxyCleared", true, type.getField("indirectProxy").get(subject) == null);
            scenario.check("proxyBaseKept", true, type.getField("proxyBase").get(subject) == proxy);
            scenario.check("childProxyKept", true, type.getField("proxy").get(child) == proxy);
            scenario.check("childIndirectProxyCleared", true, type.getField("indirectProxy").get(child) == null);
            scenario.check("childProxyBaseKept", true, type.getField("proxyBase").get(child) == proxy);
            scenario.check("staticCleared", true, type.getField("label").get(null) == null);
            scenario.check("staticMismatchCleared", true, type.getField("staticMismatch").get(null) == null);
            scenario.check("childTextKept", true, type.getField("text").get(child) == text);
            scenario.check("childInterfaceKept", true, type.getField("task").get(child) == childTask);
            scenario.check("keep", 41, subject.keep);
            scenario.check("childOwn", 17, child.own);
            Runtime.getRuntime().gc();
            scenario.check("proxyKeptAfterGc", true, type.getField("proxy").get(subject) == proxy);
            scenario.check("childProxyKeptAfterGc", true, type.getField("proxy").get(child) == proxy);
            scenario.check("textKeptAfterGc", true, type.getField("text").get(subject) == text);
            scenario.check("childInterfaceKeptAfterGc", true, type.getField("task").get(child) == childTask);
        });
        runScenario("field-retype-restore", 0, scenario -> {
            StringBuilder builder = new StringBuilder("builder");
            type.getField("text").set(subject, builder);
            scenario.redefine(new Class<?>[]{type}, "field-retype-restore");
            scenario.check("textType", "java.lang.String", type.getField("text").getType().getName());
            scenario.check("builderCleared", true, subject.text == null);
            scenario.check("proxyKept", true, subject.proxy == proxy);
            scenario.check("proxyBaseKept", true, subject.proxyBase == proxy);
            scenario.check("indirectProxyNull", true, subject.indirectProxy == null);
            scenario.check("childProxyKept", true, child.proxy == proxy);
            scenario.check("childProxyBaseKept", true, child.proxyBase == proxy);
            scenario.check("childIndirectProxyNull", true, child.indirectProxy == null);
            scenario.check("childTextKept", true, child.text == text);
            scenario.check("numberKept", true, subject.number == number);
            scenario.check("interfaceKept", true, subject.task == task);
            scenario.check("staticCleared", true, RetypeFixture.Subject.label == null);
            scenario.check("mismatchNull", true, subject.mismatch == null);
            scenario.check("staticMismatchNull", true, RetypeFixture.Subject.staticMismatch == null);
        });
    }

    private void runInvokeKindScenarios() throws Exception {
        InvokeKindFixture.Subject subject = new InvokeKindFixture.Subject();
        InvokeKindFixture.Child child = new InvokeKindFixture.Child();
        runScenario("invoke-kind-abort", 70, scenario -> {
            scenario.expectRejectionBy(ClassFinalSubject.class);
            scenario.check("before", 8, InvokeKindFixture.Caller.call(subject));
            scenario.redefine(new Class<?>[]{InvokeKindFixture.Subject.class, ClassFinalSubject.class},
                    "invoke-kind-static", "class-interface");
            scenario.check("after", 8, InvokeKindFixture.Caller.call(subject));
            scenario.check("range", 13, InvokeKindFixture.Caller.range(subject));
            scenario.check("instanceKind", false, Modifier.isStatic(
                    InvokeKindFixture.Subject.class.getMethod("convert", int.class).getModifiers()));
            scenario.check("entered", 0, InvokeKindFixture.Subject.entered);
        });
        for (int cycle = 0; cycle < 2; cycle++) {
            String suffix = cycle == 0 ? "" : "-again";
            // Resolve and warm old virtual, inherited, super and range call sites.
            for (int i = 0; i < 30000; i++) {
                if (InvokeKindFixture.Caller.call(subject) != 8 ||
                        InvokeKindFixture.Caller.inherited(child) != 8 ||
                        InvokeKindFixture.Caller.range(subject) != 13 || child.throughSuper() != 8)
                    throw new AssertionError("invoke-kind warmup");
            }
            runScenario("invoke-kind-static" + suffix, 0,
                    scenario -> checkInvokeKindMismatch(scenario, subject, child, "invoke-kind-static", true));
            runScenario("invoke-kind-caller-rebuild" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{InvokeKindFixture.CallerBase.class}, "invoke-kind-caller-rebuild");
                scenario.check("hotFailures", 30000, InvokeKindFixture.Caller.hotFailures(subject, 30000));
                scenario.expectError("oldCaller", IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.call(subject));
                scenario.check("entered", 2, InvokeKindFixture.Subject.entered);
            });
            runScenario("invoke-kind-static-callers" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{InvokeKindFixture.Caller.class, InvokeKindFixture.Child.class},
                        "invoke-kind-static", "invoke-kind-static");
                scenario.check("call", 101, InvokeKindFixture.Caller.call(subject));
                scenario.check("range", 106, InvokeKindFixture.Caller.range(subject));
                scenario.check("inherited", 101, InvokeKindFixture.Caller.inherited(child));
                scenario.check("childCall", 101, child.throughSuper());
            });
            // Resolve and compile old invoke-static and invoke-static/range sites.
            for (int i = 0; i < 30000; i++) {
                if (InvokeKindFixture.Caller.call(subject) != 101 ||
                        InvokeKindFixture.Caller.inherited(child) != 101 ||
                        InvokeKindFixture.Caller.range(subject) != 106 || child.throughSuper() != 101)
                    throw new AssertionError("invoke-static warmup");
            }
            InvokeKindFixture.Subject.entered = 0;
            runScenario("invoke-kind-instance-abort" + suffix, 70, scenario -> {
                scenario.expectRejectionBy(ClassFinalSubject.class);
                scenario.redefine(new Class<?>[]{InvokeKindFixture.Subject.class, ClassFinalSubject.class},
                        "invoke-kind-instance", "class-interface");
                scenario.check("staticKind", true, Modifier.isStatic(
                        InvokeKindFixture.Subject.class.getMethod("convert", int.class).getModifiers()));
                scenario.check("call", 101, InvokeKindFixture.Caller.call(subject));
                scenario.check("range", 106, InvokeKindFixture.Caller.range(subject));
            });
            InvokeKindFixture.Subject.entered = 0;
            runScenario("invoke-kind-instance" + suffix, 0,
                    scenario -> checkInvokeKindMismatch(scenario, subject, child, "invoke-kind-instance", false));
            runScenario("invoke-kind-static-caller-rebuild" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{InvokeKindFixture.CallerBase.class}, "invoke-kind-original");
                scenario.check("hotFailures", 30000, InvokeKindFixture.Caller.hotFailures(subject, 30000));
                scenario.expectError("oldCaller", IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.call(subject));
                scenario.check("entered", 2, InvokeKindFixture.Subject.entered);
            });
            runScenario("invoke-kind-restore" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{InvokeKindFixture.Subject.class,
                        InvokeKindFixture.Caller.class, InvokeKindFixture.Child.class, InvokeKindFixture.CallerBase.class},
                        "invoke-kind-original", "invoke-kind-original", "invoke-kind-original", "invoke-kind-original");
                scenario.check("virtual", 8, InvokeKindFixture.Caller.call(subject));
                scenario.check("range", 13, InvokeKindFixture.Caller.range(subject));
                scenario.check("inherited", 8, InvokeKindFixture.Caller.inherited(child));
                scenario.check("super", 8, child.throughSuper());
                scenario.check("marker", 7, subject.marker);
                InvokeKindFixture.Subject.entered = 0;
            });
        }
    }

    private void checkInvokeKindMismatch(Scenario scenario, InvokeKindFixture.Subject subject,
            InvokeKindFixture.Child child, String variant, boolean makeStatic) throws Exception {
        CountDownLatch ready = new CountDownLatch(2), resume = new CountDownLatch(1);
        Throwable[] activeError = new Throwable[2];
        Thread active = new Thread(() -> {
            try { InvokeKindFixture.Caller.await(subject, ready, resume); }
            catch (Throwable error) { activeError[0] = error; }
        });
        Thread activeTarget = new Thread(() -> {
            try { InvokeKindFixture.Caller.awaitTarget(subject, ready, resume); }
            catch (Throwable error) { activeError[1] = error; }
        });
        active.start(); activeTarget.start();
        if (!ready.await(10, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("active caller not ready");
        try {
            scenario.redefine(new Class<?>[]{InvokeKindFixture.Subject.class}, variant);
            resume.countDown(); active.join(10000); activeTarget.join(10000);
            scenario.check("activeCompleted", true, !active.isAlive() && !activeTarget.isAlive());
            scenario.check("activeError", IncompatibleClassChangeError.class.getName(),
                    activeError[0] == null ? "" : activeError[0].getClass().getName());
            scenario.check("activeTargetError", IncompatibleClassChangeError.class.getName(),
                    activeError[1] == null ? "" : activeError[1].getClass().getName());
            scenario.check("hotFailures", 30000, InvokeKindFixture.Caller.hotFailures(subject, 30000));
            for (int i = 0; i < 32; i++) {
                scenario.expectError("call" + i, IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.call(subject));
                scenario.expectError("inherited" + i, IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.inherited(child));
                scenario.expectError("range" + i, IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.range(subject));
                scenario.expectError("childCall" + i, IncompatibleClassChangeError.class, child::throughSuper);
            }
            scenario.check("oldCallsDidNotEnter", 0, InvokeKindFixture.Subject.entered);
            scenario.check("newCall", makeStatic ? 101 : 8, InvokeKindFixture.Subject.class.getMethod("convert", int.class).invoke(makeStatic ? null : subject, 1));
            scenario.check("newRange", makeStatic ? 106 : 13, InvokeKindFixture.Subject.class.getMethod("range", long.class, long.class, long.class).invoke(makeStatic ? null : subject, 1L, 2L, 3L));
            long before = StandardJvmtiClient.singleStepCount();
            scenario.check("singleStepEnable", 0, StandardJvmtiClient.setThreadSingleStep(true));
            try {
                scenario.expectError("interpreterCall", IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.call(subject));
                scenario.expectError("interpreterRange", IncompatibleClassChangeError.class,
                        () -> InvokeKindFixture.Caller.range(subject));
            } finally {
                scenario.check("singleStepDisable", 0, StandardJvmtiClient.setThreadSingleStep(false));
            }
            scenario.check("singleStepObserved", true, StandardJvmtiClient.singleStepCount() > before);
            System.gc();
            scenario.expectError("callAfterGc", IncompatibleClassChangeError.class,
                    () -> InvokeKindFixture.Caller.call(subject));
            scenario.expectError("rangeAfterGc", IncompatibleClassChangeError.class,
                    () -> InvokeKindFixture.Caller.range(subject));
            scenario.check("entered", 2, InvokeKindFixture.Subject.entered);
            scenario.check("marker", 7, subject.marker);
        } finally { resume.countDown(); active.join(10000); activeTarget.join(10000); }
    }

    // A started thread blocked in run() and an unstarted one of an application
    // Thread subclass keep their identity, native peer and field values.
    private void runThreadSubclassScenario() throws Exception {
        ThreadSubclassFixture running = new ThreadSubclassFixture("dcevm-thread-running");
        ThreadSubclassFixture idle = new ThreadSubclassFixture("dcevm-thread-idle");
        running.kept = 17;
        idle.kept = 23;
        running.start();
        running.started.await();
        long runningId = running.getId();
        runScenario("thread-subclass", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ThreadSubclassFixture.class}, "thread-subclass-add");
            Method added = ThreadSubclassFixture.class.getMethod("addedValue");
            scenario.check("runningKept", 17, running.kept);
            scenario.check("runningName", "dcevm-thread-running", running.getName());
            scenario.check("runningId", runningId, running.getId());
            scenario.check("runningAlive", true, running.isAlive());
            scenario.check("runningAdded", 5, added.invoke(running));
            running.interrupt();
            running.join(10_000);
            scenario.check("runningFinished", false, running.isAlive());
            scenario.check("runningInterrupted", true, running.interrupted);
            scenario.check("runningSelf", true, running.observedSelf == running);
            // The obsolete run() frame continues and resolves value() to the new body.
            scenario.check("runningResult", 2, running.result);
            scenario.check("idleKept", 23, idle.kept);
            scenario.check("idleState", Thread.State.NEW.name(), idle.getState().name());
            idle.release.countDown();
            idle.start();
            idle.join(10_000);
            scenario.check("idleFinished", false, idle.isAlive());
            scenario.check("idleSelf", true, idle.observedSelf == idle);
            scenario.check("idleResult", 25, idle.result);
            ThreadSubclassFixture created = new ThreadSubclassFixture("dcevm-thread-new");
            created.release.countDown();
            created.start();
            created.join(10_000);
            scenario.check("newResult", 25, created.result);
            Runtime.getRuntime().gc();
            scenario.check("afterGc", 17, running.kept);
        });
    }

    // Field retirement: delete every field but keep/staticKeep, restore them,
    // then change int i to long and static int si to Object.
    private void runFieldScenarios(FieldSubject fields, String schema, String values,
            Object object, Object staticObject) throws Exception {
        FieldTargets original = new FieldTargets();
        runScenario("field-retire", 0, scenario -> {
            checkFieldDefinition(scenario, fields, schema, values, object, staticObject);
            original.checkLive(scenario, fields, 7, 9_000_000_000L, 1, 7, 1);
            fields.keep = 43;
            FieldSubject.staticKeep = 44;
            scenario.redefine(new Class<?>[]{FieldSubject.class}, "field-delete");
            scenario.check("keptInstance", 43, FieldCaller.readKeep(fields));
            scenario.check("keptStatic", 44, FieldSubject.staticKeep);
            scenario.check("retiredOldCompute", 44, fields.compute());
            scenario.check("retiredNewCompute", 42, new FieldSubject().compute());
            scenario.check("remainingFields", "[keep, staticKeep]", fieldNames());
            scenario.expectError("bytecodeReadI", NoSuchFieldError.class, () -> FieldCaller.readI(fields));
            scenario.expectError("bytecodeWriteJ", NoSuchFieldError.class, () -> FieldCaller.writeJ(fields, 1));
            scenario.expectError("bytecodeReadO", NoSuchFieldError.class, () -> FieldCaller.readO(fields));
            scenario.expectError("bytecodeReadSi", NoSuchFieldError.class, FieldCaller::readSi);
            scenario.expectError("bytecodeWriteSo", NoSuchFieldError.class, () -> FieldCaller.writeSo(null));
            original.checkRetired(scenario, fields, "");
            Runtime.getRuntime().gc();
            System.runFinalization();
            original.checkRetired(scenario, fields, "AfterGc");
            scenario.check("keptAfterGc", 43, FieldCaller.readKeep(fields));
        });
        FieldTargets[] restored = new FieldTargets[1];
        runScenario("field-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{FieldSubject.class}, "field-original");
            // Restored fields are new fields: existing objects and statics start at defaults.
            scenario.check("restoredInstanceDefault", 0, FieldCaller.readI(fields));
            scenario.check("restoredReferenceDefault", true, FieldCaller.readO(fields) == null);
            scenario.check("restoredBooleanDefault", false, fields.z);
            scenario.check("restoredStaticDefault", 0, FieldCaller.readSi());
            scenario.check("restoredStaticReferenceDefault", true, FieldSubject.so == null);
            scenario.check("keptInstance", 43, fields.keep);
            scenario.check("keptStatic", 44, FieldSubject.staticKeep);
            scenario.check("freshReflection", 0,
                    FieldSubject.class.getDeclaredField("i").getInt(fields));
            original.checkRetired(scenario, fields, "Cached");
            fields.i = 17;
            fields.j = 18;
            fields.o = new Object();
            FieldSubject.si = 19;
            FieldSubject.so = new Object();
            restored[0] = new FieldTargets();
            restored[0].checkLive(scenario, fields, 17, 18, 1, 19, 1);
        });
        runScenario("field-types", 0, scenario -> {
            scenario.redefine(new Class<?>[]{FieldSubject.class}, "field-types");
            scenario.check("typeChangedI", "long", FieldSubject.class.getDeclaredField("i").getType().getName());
            scenario.check("typeChangedDefault", 0L,
                    FieldSubject.class.getDeclaredField("i").getLong(fields));
            scenario.check("storageChangedSi", "java.lang.Object",
                    FieldSubject.class.getDeclaredField("si").getType().getName());
            scenario.check("storageChangedDefault", true,
                    FieldSubject.class.getDeclaredField("si").get(null) == null);
            scenario.check("keptInstance", 43, fields.keep);
            scenario.check("oldCompute", 45, fields.compute());
            scenario.expectError("bytecodeOldTypeI", NoSuchFieldError.class, () -> FieldCaller.readI(fields));
            scenario.expectError("bytecodeOldTypeSi", NoSuchFieldError.class, FieldCaller::readSi);
            restored[0].checkRetired(scenario, fields, "Restored");
        });
    }

    private static String fieldNames() {
        List<String> names = new ArrayList<>();
        for (Field field : FieldSubject.class.getDeclaredFields()) names.add(field.getName());
        Collections.sort(names);
        return names.toString();
    }

    /** Field targets resolved while the fields exist, then used unchanged. */
    private static final class FieldTargets {
        final Field i, j, o, si, so;
        final MethodHandle getI, setJ, getSi;
        final Object varHandles;

        FieldTargets() throws Exception {
            i = FieldSubject.class.getDeclaredField("i");
            j = FieldSubject.class.getDeclaredField("j");
            o = FieldSubject.class.getDeclaredField("o");
            si = FieldSubject.class.getDeclaredField("si");
            so = FieldSubject.class.getDeclaredField("so");
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            getI = lookup.unreflectGetter(i);
            setJ = lookup.unreflectSetter(j);
            getSi = lookup.unreflectGetter(si);
            varHandles = Build.VERSION.SDK_INT >= 33 ? new FieldVarHandles() : null;
            StandardJvmtiClient.cacheFieldIds(FieldSubject.class);
        }

        void checkLive(Scenario scenario, FieldSubject fields, int iValue, long jValue,
                int oPresent, int siValue, int soPresent) throws Exception {
            scenario.check("reflectionLiveI", iValue, i.getInt(fields));
            scenario.check("reflectionLiveSi", siValue, si.getInt(null));
            scenario.check("jniLiveI", (long) iValue, StandardJvmtiClient.readCachedField(fields, 0));
            scenario.check("jniLiveJ", jValue, StandardJvmtiClient.readCachedField(fields, 1));
            scenario.check("jniLiveO", (long) oPresent, StandardJvmtiClient.readCachedField(fields, 2));
            scenario.check("jniLiveSi", (long) siValue, StandardJvmtiClient.readCachedField(fields, 3));
            scenario.check("jniLiveSo", (long) soPresent, StandardJvmtiClient.readCachedField(fields, 4));
            scenario.check("reflectedJniLiveI", (long) iValue,
                    StandardJvmtiClient.readReflectedField(i, fields, 0));
            scenario.check("handleLiveI", iValue, invoke(() -> (int) getI.invokeExact(fields)));
            scenario.check("handleLiveSi", siValue, invoke(() -> (int) getSi.invokeExact()));
            if (varHandles != null) ((FieldVarHandles) varHandles).checkLive(scenario, fields, iValue, siValue);
        }

        void checkRetired(Scenario scenario, FieldSubject fields, String suffix) throws Exception {
            scenario.expectError("reflectionI" + suffix, IllegalArgumentException.class, () -> i.getInt(fields));
            scenario.expectError("reflectionJ" + suffix, IllegalArgumentException.class, () -> j.getLong(fields));
            scenario.expectError("reflectionO" + suffix, IllegalArgumentException.class, () -> o.get(fields));
            scenario.expectError("reflectionSetI" + suffix, IllegalArgumentException.class,
                    () -> i.setInt(fields, 1));
            scenario.expectError("reflectionSi" + suffix, NoClassDefFoundError.class, () -> si.getInt(null));
            scenario.expectError("reflectionSetSo" + suffix, NoClassDefFoundError.class, () -> so.set(null, null));
            for (int field = 0; field < 5; field++) {
                final int index = field;
                scenario.expectError("jni" + field + suffix, NoSuchFieldError.class,
                        () -> StandardJvmtiClient.readCachedField(fields, index));
            }
            scenario.expectError("jniWriteJ" + suffix, NoSuchFieldError.class,
                    () -> StandardJvmtiClient.writeCachedField(fields, 1, 5));
            scenario.expectError("reflectedJniI" + suffix, NoSuchFieldError.class,
                    () -> StandardJvmtiClient.readReflectedField(i, fields, 0));
            scenario.expectError("reflectedJniSi" + suffix, NoSuchFieldError.class,
                    () -> StandardJvmtiClient.readReflectedField(si, fields, 3));
            scenario.expectError("handleGetI" + suffix, NoSuchFieldError.class, () -> {
                int ignored = (int) getI.invokeExact(fields);
            });
            scenario.expectError("handleSetJ" + suffix, NoSuchFieldError.class,
                    () -> setJ.invokeExact(fields, 5L));
            scenario.expectError("handleGetSi" + suffix, NoSuchFieldError.class, () -> {
                int ignored = (int) getSi.invokeExact();
            });
            if (varHandles != null) ((FieldVarHandles) varHandles).checkRetired(scenario, fields, suffix);
        }

        private interface IntCall {
            int call() throws Throwable;
        }

        private static int invoke(IntCall call) throws Exception {
            try {
                return call.call();
            } catch (Throwable error) {
                if (error instanceof Exception) throw (Exception) error;
                if (error instanceof Error) throw (Error) error;
                throw new RuntimeException(error);
            }
        }
    }

    /** VarHandle field access exists from API 33. */
    @android.annotation.TargetApi(33)
    private static final class FieldVarHandles {
        final VarHandle i, si;

        FieldVarHandles() throws Exception {
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            i = lookup.findVarHandle(FieldSubject.class, "i", int.class);
            si = lookup.findStaticVarHandle(FieldSubject.class, "si", int.class);
        }

        void checkLive(Scenario scenario, FieldSubject fields, int iValue, int siValue) throws Exception {
            scenario.check("varHandleLiveI", iValue, (int) i.get(fields));
            scenario.check("varHandleLiveSi", siValue, (int) si.get());
        }

        void checkRetired(Scenario scenario, FieldSubject fields, String suffix) throws Exception {
            scenario.expectError("varHandleI" + suffix, UnsupportedOperationException.class, () -> {
                int ignored = (int) i.get(fields);
            });
            scenario.expectError("varHandleSetI" + suffix, UnsupportedOperationException.class,
                    () -> i.set(fields, 1));
            scenario.expectError("varHandleSi" + suffix, UnsupportedOperationException.class, () -> {
                int ignored = (int) si.get();
            });
        }
    }

    private void checkClassFinalUnchanged(Scenario scenario, ClassFinalSubject classFinal,
            int flags, String schema) throws Exception {
        scenario.check("classFlags", flags, ClassFinalSubject.class.getModifiers());
        scenario.check("unchangedMembers", schema, memberSchema(ClassFinalSubject.class));
        scenario.check("oldValue", 29, classFinal.value());
        scenario.check("newValue", 29, new ClassFinalSubject().value());
    }

    private void runPointerIdScenarios() throws Exception {
        PointerIdFixture.Implementation instance = new PointerIdFixture.Implementation();
        PointerIdFixture.Child child = new PointerIdFixture.Child();
        instance.marker = 73;
        int[] stages = {19, 20};
        for (int stage : stages) {
            runScenario(stage == 19 ? "interface-pointer-target" : "interface-pointer-subtype", 79, scenario -> {
                scenario.expectRejectionBy(stage == 19 ? PointerIdFixture.Contract.class : PointerIdFixture.Implementation.class);
                scenario.check("arm", 0, InterfaceAbortInstrumentation.armPointerFailure(stage));
                scenario.redefine(new Class<?>[]{PointerIdFixture.Contract.class}, "interface-pointer-body");
                scenario.check("rejectionReason", true,
                        DcevmRuntime.capabilities().getString("lastRejection").contains("active pointer JNI IDs"));
                scenario.check("old", 51, instance.pointerValue());
                scenario.check("child", 51, child.pointerValue());
                scenario.check("new", 51, new PointerIdFixture.Implementation().pointerValue());
                scenario.check("marker", 73, instance.marker);
            });
        }
        runScenario("interface-pointer-recovery", 0, scenario -> {
            scenario.redefine(new Class<?>[]{PointerIdFixture.Contract.class}, "interface-pointer-body");
            scenario.check("old", 52, instance.pointerValue());
            scenario.check("child", 52, child.pointerValue());
            scenario.check("new", 52, new PointerIdFixture.Implementation().pointerValue());
            scenario.check("marker", 73, instance.marker);
        });
    }

    private void runMultiInterfaceScenarios() throws Exception {
        MultiInterfaceFixture.LeftImpl left = new MultiInterfaceFixture.LeftImpl();
        MultiInterfaceFixture.RightImpl right = new MultiInterfaceFixture.RightImpl();
        left.marker = 73; right.marker = 79;
        runScenario("multi-interface-unrelated", 0, scenario -> {
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Left.class, MultiInterfaceFixture.Right.class},
                    "multi-interface-body", "multi-interface-body");
            scenario.check("left", 12, left.leftValue());
            scenario.check("right", 22, right.rightValue());
            scenario.check("newLeft", 12, new MultiInterfaceFixture.LeftImpl().leftValue());
            scenario.check("newRight", 22, new MultiInterfaceFixture.RightImpl().rightValue());
            scenario.check("leftMarker", 73, left.marker);
            scenario.check("rightMarker", 79, right.marker);
            scenario.check("allocatedOnce", 4, InterfaceAbortInstrumentation.allocatedTypes());
        });
        runScenario("multi-interface-unimplemented", 0, scenario -> {
            // Right gains an abstract method that RightImpl lacks.
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Left.class, MultiInterfaceFixture.Right.class},
                    "multi-interface-next", "multi-interface-invalid");
            scenario.check("left", 13, left.leftValue());
            scenario.check("right", 22, right.rightValue());
            scenario.expectError("abstractCall", AbstractMethodError.class,
                    () -> MultiInterfaceFixture.Right.class.getMethod("added").invoke(right));
            scenario.check("leftMarker", 73, left.marker);
            scenario.check("rightMarker", 79, right.marker);
        });
        runScenario("multi-interface-recovery", 0, scenario -> {
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Right.class, MultiInterfaceFixture.Left.class},
                    "multi-interface-next", "multi-interface-next");
            scenario.check("left", 13, left.leftValue());
            scenario.check("right", 23, right.rightValue());
            scenario.check("allocatedOnce", 4, InterfaceAbortInstrumentation.allocatedTypes());
        });
        MultiInterfaceFixture.FamilyImpl family = new MultiInterfaceFixture.FamilyImpl();
        MultiInterfaceFixture.FamilyChild child = new MultiInterfaceFixture.FamilyChild();
        MultiInterfaceFixture.Parent parentView = family;
        MultiInterfaceFixture.Child childView = family;
        family.marker = 83; child.marker = 89; child.childMarker = 97;
        runScenario("multi-interface-family", 0, scenario -> {
            // Child precedes parent in the input and ART's equal-depth sort.
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Child.class, MultiInterfaceFixture.Parent.class},
                    "multi-interface-body", "multi-interface-body");
            scenario.check("childArrivedFirst", 1, InterfaceAbortInstrumentation.allocationOrder());
            scenario.check("parent", 32, parentView.parentValue());
            scenario.check("child", 42, childView.childValue());
            scenario.check("virtualParent", 32, family.parentValue());
            scenario.check("virtualChild", 42, family.childValue());
            scenario.check("subclassParent", 32, child.parentValue());
            scenario.check("subclassChild", 42, child.childValue());
            scenario.check("newParent", 32, new MultiInterfaceFixture.FamilyImpl().parentValue());
            scenario.check("newChild", 42, new MultiInterfaceFixture.FamilyChild().childValue());
            scenario.check("marker", 83, family.marker);
            scenario.check("subclassMarker", 89, child.marker);
            scenario.check("childMarker", 97, child.childMarker);
            scenario.check("allocatedOnce", 4, InterfaceAbortInstrumentation.allocatedTypes());
            scenario.check("snapshotOnce", 4L, DcevmRuntime.capabilities().getLong("interfaceSnapshotTypes"));
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("parentAfterGc", 32, parentView.parentValue());
            scenario.check("childAfterGc", 42, childView.childValue());
        });
        // Keep this type out of verifier type_ids until the disjoint-group checks finish.
        Object shared = Class.forName("com.github.ytlog.artdcevm.fixture.MultiInterfaceFixture$SharedImpl")
                .getConstructor().newInstance();
        MultiInterfaceFixture.Left sharedLeft = (MultiInterfaceFixture.Left) shared;
        MultiInterfaceFixture.Right sharedRight = (MultiInterfaceFixture.Right) shared;
        shared.getClass().getField("marker").setInt(shared, 37);
        runScenario("multi-interface-shared", 0, scenario -> {
            // No interface covers every instance: ART collects each root's instances.
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Left.class, MultiInterfaceFixture.Right.class},
                    "multi-interface-body", "multi-interface-body");
            scenario.check("left", 12, left.leftValue());
            scenario.check("right", 22, right.rightValue());
            scenario.check("sharedLeft", 12, sharedLeft.leftValue());
            scenario.check("sharedRight", 22, sharedRight.rightValue());
            scenario.check("newSharedLeft", 12, ((MultiInterfaceFixture.Left) shared.getClass().getConstructor().newInstance()).leftValue());
            scenario.check("newSharedRight", 22, ((MultiInterfaceFixture.Right) shared.getClass().getConstructor().newInstance()).rightValue());
            scenario.check("sharedMarker", 37, shared.getClass().getField("marker").getInt(shared));
            scenario.check("leftMarker", 73, left.marker);
            scenario.check("rightMarker", 79, right.marker);
            scenario.check("allocatedOnce", 5, InterfaceAbortInstrumentation.allocatedTypes());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("rightAfterGc", 22, right.rightValue());
            scenario.check("sharedRightAfterGc", 22, sharedRight.rightValue());
        });
        runScenario("multi-interface-after-shared", 0, scenario -> {
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.Left.class}, "multi-interface-next");
            scenario.check("left", 13, left.leftValue());
            scenario.check("rightUnchanged", 22, right.rightValue());
            scenario.check("sharedLeft", 13, sharedLeft.leftValue());
            scenario.check("sharedRight", 22, sharedRight.rightValue());
        });
        MultiInterfaceFixture.CoveredLeftOnly coveredOnly = new MultiInterfaceFixture.CoveredLeftOnly();
        MultiInterfaceFixture.CoveredBoth coveredBoth = new MultiInterfaceFixture.CoveredBoth();
        MultiInterfaceFixture.CoveredRight coveredRight = coveredBoth;
        coveredOnly.marker = 47; coveredBoth.marker = 53;
        runScenario("multi-interface-covered", 0, scenario -> {
            // CoveredRight leads the batch, but only CoveredLeft covers both instances.
            scenario.redefine(new Class<?>[]{MultiInterfaceFixture.CoveredRight.class, MultiInterfaceFixture.CoveredLeft.class},
                    "multi-interface-covered", "multi-interface-covered");
            scenario.check("leftOnly", 52, coveredOnly.coveredLeft());
            scenario.check("bothLeft", 52, coveredBoth.coveredLeft());
            scenario.check("bothRight", 62, coveredRight.coveredRight());
            scenario.check("newBothRight", 62, new MultiInterfaceFixture.CoveredBoth().coveredRight());
            scenario.check("leftOnlyMarker", 47, coveredOnly.marker);
            scenario.check("bothMarker", 53, coveredBoth.marker);
            scenario.check("allocatedOnce", 4, InterfaceAbortInstrumentation.allocatedTypes());
            scenario.check("snapshotOnce", 4L, DcevmRuntime.capabilities().getLong("interfaceSnapshotTypes"));
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("bothRightAfterGc", 62, coveredRight.coveredRight());
        });
    }

    private void runInterfaceOverlapScenarios() throws Exception {
        InterfaceOverlapFixture.Implementation instance = new InterfaceOverlapFixture.Implementation();
        InterfaceOverlapFixture.Contract contract = instance;
        instance.marker = 73;
        runScenario("interface-overlap-body", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceOverlapFixture.Parent.class, InterfaceOverlapFixture.Contract.class},
                    "interface-overlap-body", "interface-overlap-body");
            scenario.check("interfaceAfter", 32, contract.value());
            scenario.check("parentAfter", 22, instance.parentValue());
            scenario.check("newInterface", 32, new InterfaceOverlapFixture.Implementation().value());
            scenario.check("newParent", 22, new InterfaceOverlapFixture.Implementation().parentValue());
            scenario.check("marker", 73, instance.marker);
        });
        String schema = memberSchema(InterfaceOverlapFixture.Parent.class);
        runScenario("interface-overlap-structural", 0, scenario -> {
            // Parent is outside Contract's type relation but rebuilds Implementation too.
            scenario.redefine(new Class<?>[]{InterfaceOverlapFixture.Contract.class, InterfaceOverlapFixture.Parent.class},
                    "interface-overlap-structural", "interface-overlap-structural");
            scenario.check("parentChanged", true, !schema.equals(memberSchema(InterfaceOverlapFixture.Parent.class)));
            scenario.check("interfaceAfter", 33, contract.value());
            scenario.check("parentAfter", 23, instance.parentValue());
            scenario.check("existingAdded", 0, InterfaceOverlapFixture.Parent.class.getField("added").getInt(instance));
            InterfaceOverlapFixture.Implementation created = new InterfaceOverlapFixture.Implementation();
            scenario.check("newAdded", 19, InterfaceOverlapFixture.Parent.class.getField("added").getInt(created));
            scenario.check("newInterface", 33, created.value());
            scenario.check("newParent", 23, created.parentValue());
            scenario.check("marker", 73, instance.marker);
            scenario.check("allocatedOnce", 3, InterfaceAbortInstrumentation.allocatedTypes());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("interfaceAfterGc", 33, contract.value());
        });
        runScenario("interface-overlap-recovery", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceOverlapFixture.Contract.class}, "interface-overlap-next");
            scenario.check("interfaceAfter", 34, contract.value());
            scenario.check("parentAfter", 23, instance.parentValue());
            scenario.check("existingAdded", 0, InterfaceOverlapFixture.Parent.class.getField("added").getInt(instance));
            scenario.check("marker", 73, instance.marker);
        });
        String superclassSchema = memberSchema(InterfaceOverlapFixture.Parent.class);
        runScenario("interface-overlap-superclass", 66, scenario -> {
            scenario.expectRejectionBy(InterfaceOverlapFixture.Parent.class);
            scenario.redefine(new Class<?>[]{InterfaceOverlapFixture.Contract.class, InterfaceOverlapFixture.Parent.class},
                    "interface-overlap-superclass", "interface-overlap-superclass");
            scenario.check("rejectionReason", true,
                    DcevmRuntime.capabilities().getString("lastRejection").contains("superclass change overlaps"));
            scenario.check("unchangedParent", superclassSchema, memberSchema(InterfaceOverlapFixture.Parent.class));
            scenario.check("superclassUnchanged", "java.lang.Object",
                    InterfaceOverlapFixture.Parent.class.getSuperclass().getName());
            scenario.check("interfaceUnchanged", 34, contract.value());
            scenario.check("parentUnchanged", 23, instance.parentValue());
            scenario.check("marker", 73, instance.marker);
        });
        Object other = Class.forName("com.github.ytlog.artdcevm.fixture.InterfaceOverlapFixture$OtherImplementation")
                .getConstructor().newInstance();
        InterfaceOverlapFixture.Contract otherContract = (InterfaceOverlapFixture.Contract) other;
        InterfaceOverlapFixture.Parent plainParent = new InterfaceOverlapFixture.Parent();
        InterfaceOverlapFixture.Parent.class.getField("added").setInt(plainParent, 61);
        runScenario("interface-overlap-uncovered", 0, scenario -> {
            // Neither Contract nor Parent covers every instance: ART collects both.
            scenario.redefine(new Class<?>[]{InterfaceOverlapFixture.Contract.class, InterfaceOverlapFixture.Parent.class},
                    "interface-overlap-uncovered", "interface-overlap-uncovered");
            scenario.check("interfaceAfter", 36, contract.value());
            scenario.check("otherAfter", 36, otherContract.value());
            scenario.check("parentAfter", 23, instance.parentValue());
            scenario.check("existingAnother", 0L, InterfaceOverlapFixture.Parent.class.getField("another").getLong(instance));
            scenario.check("newAnother", 29L, InterfaceOverlapFixture.Parent.class.getField("another")
                    .getLong(new InterfaceOverlapFixture.Implementation()));
            scenario.check("plainAdded", 61, InterfaceOverlapFixture.Parent.class.getField("added").getInt(plainParent));
            scenario.check("plainAnother", 0L, InterfaceOverlapFixture.Parent.class.getField("another").getLong(plainParent));
            scenario.check("plainValue", 23, plainParent.parentValue());
            scenario.check("marker", 73, instance.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("plainAfterGc", 61, InterfaceOverlapFixture.Parent.class.getField("added").getInt(plainParent));
        });
    }

    private void runInterfaceImplementationScenarios() throws Exception {
        InterfaceBatchFixture.Implementation instance = new InterfaceBatchFixture.Implementation();
        InterfaceBatchFixture.Contract contract = instance;
        InterfaceBatchFixture.Child child = new InterfaceBatchFixture.Child();
        InterfaceBatchFixture.Contract childContract = child;
        instance.marker = 73; child.marker = 79; child.childMarker = 83;
        runScenario("interface-implementation-body", 0, scenario -> {
            scenario.check("interfaceBefore", 12, contract.value());
            scenario.check("ownBefore", 104, instance.ownValue());
            // Equal depth, two entries: ART's two-element sort keeps this order.
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                    "interface-implementation-body", "interface-implementation-body");
            scenario.check("implementationAllocatedFirst", 1, InterfaceAbortInstrumentation.allocationOrder());
            scenario.check("interfaceAfter", 14, contract.value());
            scenario.check("ownAfter", 114, instance.ownValue());
            scenario.check("childInterface", 14, childContract.value());
            scenario.check("childOwn", 120, child.ownValue());
            scenario.check("newInterface", 14, new InterfaceBatchFixture.Implementation().value());
            scenario.check("newOwn", 58, new InterfaceBatchFixture.Implementation().ownValue());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("interface-implementation-add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Contract.class, InterfaceBatchFixture.Implementation.class},
                    "interface-implementation-add", "interface-implementation-add");
            scenario.check("interfaceAfter", 15, contract.value());
            scenario.check("ownAfter", 115, instance.ownValue());
            scenario.check("marker", 73, instance.marker);
            Field field = InterfaceBatchFixture.Implementation.class.getDeclaredField("added");
            Method method = InterfaceBatchFixture.Implementation.class.getDeclaredMethod("addedValue");
            InterfaceBatchFixture.Implementation fresh = new InterfaceBatchFixture.Implementation();
            scenario.check("oldAdded", 0, field.getInt(instance));
            scenario.check("childAdded", 0, field.getInt(child));
            scenario.check("newAdded", 23, field.getInt(fresh));
            scenario.check("oldAddedMethod", 124, method.invoke(instance));
            scenario.check("newAddedMethod", 91, method.invoke(fresh));
        });
        runScenario("interface-implementation-child", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Child.class, InterfaceBatchFixture.Implementation.class,
                            InterfaceBatchFixture.Contract.class},
                    "interface-implementation-child", "interface-implementation-child", "interface-implementation-child");
            scenario.check("implementationAllocatedFirst", 1, InterfaceAbortInstrumentation.allocationOrder());
            scenario.check("interfaceAfter", 16, contract.value());
            scenario.check("ownAfter", 116, instance.ownValue());
            scenario.check("childInterface", 16, childContract.value());
            scenario.check("childOwn", 122, child.ownValue());
            scenario.check("childBody", 154, child.childValue());
            InterfaceBatchFixture.Child freshChild = new InterfaceBatchFixture.Child();
            scenario.check("newChildBody", 100, freshChild.childValue());
            scenario.check("newChildInterface", 16, freshChild.value());
            scenario.check("newChildOwn", 60, freshChild.ownValue());
            scenario.check("marker", 73, instance.marker);
            scenario.check("childMarker", 79, child.marker);
            scenario.check("ownChildMarker", 83, child.childMarker);
            scenario.check("childAdded", 0, InterfaceBatchFixture.Implementation.class.getDeclaredField("added").getInt(child));
        });
        runScenario("interface-implementation-child-only", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Contract.class, InterfaceBatchFixture.Child.class},
                    "interface-implementation-child-only", "interface-implementation-child-only");
            scenario.check("childBody", 155, child.childValue());
            scenario.check("childInterface", 16, childContract.value());
            scenario.check("ownAfter", 116, instance.ownValue());
            scenario.check("childMarker", 79, child.marker);
            scenario.check("ownChildMarker", 83, child.childMarker);
        });
        Method cached = InterfaceBatchFixture.Implementation.class.getDeclaredMethod("addedValue");
        Field cachedField = InterfaceBatchFixture.Implementation.class.getDeclaredField("added");
        runScenario("interface-implementation-retire", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                    "interface-implementation-retire", "interface-implementation-retire");
            scenario.check("interfaceAfter", 17, contract.value());
            scenario.check("ownAfter", 119, instance.ownValue());
            scenario.check("methodModifiers", 33, InterfaceBatchFixture.Implementation.class.getDeclaredMethod("ownValue").getModifiers());
            scenario.expectError("removedMethod", NoSuchMethodException.class,
                    () -> InterfaceBatchFixture.Implementation.class.getDeclaredMethod("addedValue"));
            scenario.expectReflectionError("cachedMethod", () -> cached.invoke(instance));
            scenario.expectError("removedField", NoSuchFieldException.class,
                    () -> InterfaceBatchFixture.Implementation.class.getDeclaredField("added"));
            scenario.expectError("cachedField", IllegalArgumentException.class, () -> cachedField.getInt(instance));
            scenario.check("marker", 73, instance.marker);
            scenario.check("childInterface", 17, childContract.value());
        });
        String contractSchema = memberSchema(InterfaceBatchFixture.Contract.class);
        String implementationSchema = memberSchema(InterfaceBatchFixture.Implementation.class);
        String[] failures = {"invalid", "abstract"};
        int[] codes = {70, 70};
        for (int i = 0; i < failures.length; ++i) {
            String variant = "interface-implementation-" + failures[i];
            String reason = i == 0 ? "subclass" : "live instances";
            runScenario(variant, codes[i], scenario -> {
                scenario.expectRejectionBy(InterfaceBatchFixture.Implementation.class);
                scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                        variant, variant);
                scenario.check("rejectionReason", true, DcevmRuntime.capabilities().getString("lastRejection").contains(reason));
                scenario.check("unchangedInterface", contractSchema, memberSchema(InterfaceBatchFixture.Contract.class));
                scenario.check("unchangedImplementation", implementationSchema, memberSchema(InterfaceBatchFixture.Implementation.class));
                scenario.check("interfaceUnchanged", 17, contract.value());
                scenario.check("ownUnchanged", 119, instance.ownValue());
                scenario.check("childUnchanged", 17, childContract.value());
                scenario.check("marker", 73, instance.marker);
                scenario.check("newInterfaceUnchanged", 17, new InterfaceBatchFixture.Implementation().value());
            });
        }
        runScenario("interface-implementation-recovery", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Contract.class}, "interface-implementation-next");
            scenario.check("interfaceAfter", 18, contract.value());
            scenario.check("ownAfter", 119, instance.ownValue());
            scenario.check("childInterface", 18, childContract.value());
            scenario.check("marker", 73, instance.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 18, contract.value());
        });
    }

    private void runInterfaceBatchScenarios() throws Exception {
        InterfaceBatchFixture.Implementation implementation = new InterfaceBatchFixture.Implementation();
        InterfaceBatchFixture.Contract contract = implementation;
        InterfaceBatchFixture.Unrelated unrelated = new InterfaceBatchFixture.Unrelated();
        implementation.marker = 73;
        unrelated.marker = 79;
        runScenario("interface-batch-unrelated", 0, scenario -> {
            scenario.check("interfaceBefore", 11, contract.value());
            scenario.check("unrelatedBefore", 21, unrelated.value());
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Contract.class, InterfaceBatchFixture.Unrelated.class},
                    "interface-batch-body", "interface-batch-body");
            scenario.check("interfaceAfter", 12, contract.value());
            scenario.check("virtualAfter", 12, implementation.value());
            scenario.check("unrelatedAfter", 22, unrelated.value());
            scenario.check("newInterface", 12, new InterfaceBatchFixture.Implementation().value());
            scenario.check("newUnrelated", 22, new InterfaceBatchFixture.Unrelated().value());
            scenario.check("interfaceMarker", 73, implementation.marker);
            scenario.check("unrelatedMarker", 79, unrelated.marker);
        });
        String interfaceSchema = memberSchema(InterfaceBatchFixture.Contract.class);
        String unrelatedSchema = memberSchema(InterfaceBatchFixture.Unrelated.class);
        runScenario("interface-batch-unrelated-invalid", 70, scenario -> {
            scenario.expectRejectionBy(InterfaceBatchFixture.Unrelated.class);
            // Reverse input order checks rejection independently of target position.
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Unrelated.class, InterfaceBatchFixture.Contract.class},
                    "interface-batch-invalid", "interface-batch-next");
            scenario.check("rejectionReason", true,
                    DcevmRuntime.capabilities().getString("lastRejection").contains("live instances"));
            scenario.check("unchangedInterface", interfaceSchema, memberSchema(InterfaceBatchFixture.Contract.class));
            scenario.check("unchangedUnrelated", unrelatedSchema, memberSchema(InterfaceBatchFixture.Unrelated.class));
            scenario.check("interfaceUnchanged", 12, contract.value());
            scenario.check("unrelatedUnchanged", 22, unrelated.value());
            scenario.check("newInterfaceUnchanged", 12, new InterfaceBatchFixture.Implementation().value());
            scenario.check("newUnrelatedUnchanged", 22, new InterfaceBatchFixture.Unrelated().value());
            scenario.check("interfaceMarker", 73, implementation.marker);
            scenario.check("unrelatedMarker", 79, unrelated.marker);
        });
        runScenario("interface-batch-after-rejection", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Contract.class}, "interface-batch-next");
            scenario.check("interfaceAfter", 13, contract.value());
            scenario.check("unrelatedUnchanged", 22, unrelated.value());
            scenario.check("interfaceMarker", 73, implementation.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 13, contract.value());
        });
        runScenario("interface-batch-unrelated-reverse", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceBatchFixture.Unrelated.class, InterfaceBatchFixture.Contract.class},
                    "interface-batch-body", "interface-batch-body");
            scenario.check("interfaceAfter", 12, contract.value());
            scenario.check("unrelatedAfter", 22, unrelated.value());
            scenario.check("interfaceMarker", 73, implementation.marker);
            scenario.check("unrelatedMarker", 79, unrelated.marker);
        });
    }

    private void runNestedInterfaceScenarios() throws Exception {
        NestedInterfaceHost.Implementation instance = new NestedInterfaceHost.Implementation();
        NestedInterfaceHost.Contract contract = instance;
        instance.marker = 73;
        InterfaceMethodFixture.Implementation bodies = new InterfaceMethodFixture.Implementation();
        bodies.marker = 73;
        runScenario("interface-method-bodies", 0, scenario -> {
            scenario.check("defaultBefore", 32, bodies.value());
            scenario.check("staticBefore", 41, InterfaceMethodFixture.Contract.staticValue());
            Method before = InterfaceMethodFixture.Contract.class.getDeclaredMethod("hidden");
            before.setAccessible(true);
            scenario.check("privateBefore", 31, before.invoke(bodies));
            scenario.redefine(new Class<?>[]{InterfaceMethodFixture.Contract.class}, "interface-method-bodies");
            scenario.check("oldDefault", 34, bodies.value());
            scenario.check("newDefault", 34, new InterfaceMethodFixture.Implementation().value());
            scenario.check("staticAfter", 42, InterfaceMethodFixture.Contract.staticValue());
            Method after = InterfaceMethodFixture.Contract.class.getDeclaredMethod("hidden");
            after.setAccessible(true);
            scenario.check("privateAfter", 32, after.invoke(bodies));
            scenario.check("marker", 73, bodies.marker);
            scenario.check("interfaceCapability", true,
                    DcevmRuntime.capabilities().getBoolean("interfaceRedefinition"));
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 34, bodies.value());
        });
        runScenario("nested-interface-body", 0, scenario -> {
            scenario.check("innerStatic", true, Modifier.isStatic(NestedInterfaceHost.Contract.class.getModifiers()));
            scenario.check("before", 41, contract.value());
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-body");
            scenario.check("oldInterface", 42, contract.value());
            scenario.check("oldVirtual", 42, instance.value());
            scenario.check("newInstance", 42, new NestedInterfaceHost.Implementation().value());
            scenario.check("marker", 73, instance.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 42, contract.value());
        });
        runScenario("nested-interface-add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-add");
            scenario.expectError("addedCall", AbstractMethodError.class,
                    () -> NestedInterfaceHost.Contract.class.getMethod("added").invoke(contract));
            scenario.check("value", 42, contract.value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-modifiers", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-modifiers");
            scenario.expectError("interfaceCall", AbstractMethodError.class, contract::value);
            scenario.expectError("virtualCall", AbstractMethodError.class, instance::value);
            scenario.expectError("newCall", AbstractMethodError.class,
                    () -> new NestedInterfaceHost.Implementation().value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-parents", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-parents");
            scenario.check("isRunnable", true, Runnable.class.isInstance(instance));
            scenario.expectError("parentCall", AbstractMethodError.class, () -> Runnable.class.cast(instance).run());
            scenario.check("value", 44, contract.value());
            scenario.check("virtualValue", 44, instance.value());
            scenario.check("newValue", 44, new NestedInterfaceHost.Implementation().value());
            scenario.check("marker", 73, instance.marker);
        });
        // Member evolution of the default: each step is restored to value() == 42.
        Method cachedValue = NestedInterfaceHost.Contract.class.getMethod("value");
        runScenario("nested-interface-signature", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-signature");
            Method resigned = NestedInterfaceHost.Contract.class.getMethod("value", int.class);
            scenario.check("newSignature", 45, resigned.invoke(contract, 1));
            scenario.check("newSignatureVirtual", 46, resigned.invoke(instance, 2));
            scenario.expectError("oldInterfaceCall", NoSuchMethodError.class, contract::value);
            scenario.expectError("oldVirtualCall", NoSuchMethodError.class, instance::value);
            scenario.expectError("oldReflection", NoSuchMethodError.class, () -> cachedValue.invoke(contract));
            scenario.check("marker", 73, instance.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("newSignatureAfterGc", 45, resigned.invoke(contract, 1));
        });
        runScenario("nested-interface-restore-signature", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-body");
            scenario.check("restored", 42, contract.value());
            scenario.check("restoredVirtual", 42, instance.value());
            scenario.expectError("retiredReflection", NoSuchMethodError.class, () -> cachedValue.invoke(contract));
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-delete", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-delete");
            scenario.check("declaredMethods", 0, NestedInterfaceHost.Contract.class.getDeclaredMethods().length);
            scenario.expectError("interfaceCall", NoSuchMethodError.class, contract::value);
            scenario.expectError("virtualCall", NoSuchMethodError.class, instance::value);
            scenario.expectError("newInstanceCall", NoSuchMethodError.class,
                    () -> new NestedInterfaceHost.Implementation().value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-restore-delete", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-body");
            scenario.check("restored", 42, contract.value());
            scenario.check("restoredVirtual", 42, instance.value());
            scenario.check("restoredNew", 42, new NestedInterfaceHost.Implementation().value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-field", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-field");
            // The interface was initialized before redefinition: the added static keeps its default.
            scenario.check("addedField", 0, NestedInterfaceHost.Contract.class.getField("added").getInt(null));
            scenario.check("value", 44, contract.value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-restore-field", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-body");
            scenario.check("fieldRetired", 0, NestedInterfaceHost.Contract.class.getFields().length);
            scenario.check("restored", 42, contract.value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-initializer", 0, scenario -> {
            // As for a class, an initializer added to an initialized interface never runs.
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-initializer");
            scenario.check("initializerNotRun", true, NestedInterfaceHost.Contract.class.getField("NAMES").get(null) == null);
            scenario.check("value", 47, contract.value());
            scenario.check("marker", 73, instance.marker);
        });
        runScenario("nested-interface-restore-initializer", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NestedInterfaceHost.Contract.class}, "nested-interface-body");
            scenario.check("fieldRetired", 0, NestedInterfaceHost.Contract.class.getFields().length);
            scenario.check("restored", 42, contract.value());
            scenario.check("marker", 73, instance.marker);
        });
        CopiedChild child = new CopiedChild();
        CopiedDefault topLevel = child;
        runScenario("interface-after-nested", 0, scenario -> {
            scenario.redefine(new Class<?>[]{CopiedDefault.class}, "interface-default-body");
            scenario.check("topLevelDispatch", 93, topLevel.copiedValue());
            scenario.check("nestedDispatch", 42, contract.value());
            scenario.check("runtimeAvailable", true,
                    DcevmRuntime.capabilities().getString("interfaceRuntime").startsWith("verified: "));
        });
        runInterfaceKindScenarios();
    }

    // abstract <-> default keeps the method identity.
    private void runInterfaceKindScenarios() throws Exception {
        InterfaceKindFixture.Concrete concrete = new InterfaceKindFixture.Concrete();
        InterfaceKindFixture.Contract contract = concrete;
        concrete.marker = 7;
        runScenario("interface-kind-default", 0, scenario -> {
            scenario.check("requiredBefore", 41, contract.required());
            // Moving an implementation into a new default is one atomic batch.
            scenario.redefine(new Class<?>[]{InterfaceKindFixture.Concrete.class, InterfaceKindFixture.Contract.class},
                    "interface-kind-default", "interface-kind-default");
            scenario.check("required", 12, contract.required());
            scenario.check("virtualRequired", 12, concrete.required());
            scenario.check("newRequired", 12, new InterfaceKindFixture.Concrete().required());
            scenario.check("optional", 11, contract.optional());
            scenario.check("defaultFlag", true, InterfaceKindFixture.Contract.class.getMethod("required").isDefault());
            scenario.check("marker", 7, concrete.marker);
        });
        runScenario("interface-kind-abstract-missing", 0, scenario -> {
            // The default becomes abstract while Concrete has no implementation.
            scenario.redefine(new Class<?>[]{InterfaceKindFixture.Contract.class}, "interface-kind-abstract-missing");
            scenario.expectError("abstractCall", AbstractMethodError.class, () -> contract.optional());
            scenario.check("abstractFlag", true,
                    Modifier.isAbstract(InterfaceKindFixture.Contract.class.getMethod("optional").getModifiers()));
            scenario.check("marker", 7, concrete.marker);
        });
        runScenario("interface-kind-abstract", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceKindFixture.Contract.class, InterfaceKindFixture.Concrete.class},
                    "interface-kind-abstract", "interface-kind-abstract");
            scenario.check("optional", 51, contract.optional());
            scenario.check("virtualOptional", 51, concrete.optional());
            scenario.check("abstractFlag", true,
                    Modifier.isAbstract(InterfaceKindFixture.Contract.class.getMethod("optional").getModifiers()));
            scenario.check("required", 12, contract.required());
            scenario.check("marker", 7, concrete.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("optionalAfterGc", 51, contract.optional());
        });
        runInterfaceParentScenarios();
        runInterfaceAccessScenarios();
        runInterfaceInvokeKindScenarios();
    }

    // Static/default/private changes protect external callers; old active target
    // frames retain a separate 71 boundary for virtual-to-private dispatch.
    private void runInterfaceInvokeKindScenarios() throws Exception {
        Class<?> type = InterfaceInvokeKindFixture.Contract.class;
        InterfaceInvokeKindFixture.Impl impl = new InterfaceInvokeKindFixture.Impl();
        impl.marker = 9;
        if (InterfaceInvokeKindFixture.Caller.call(impl) != 11) throw new AssertionError("Caller precondition");
        Method cachedValue = type.getMethod("value");
        runScenario("interface-kind-static-legacy", 0, scenario -> {
            scenario.check("interfaceBefore", 11, InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
            scenario.redefine(new Class<?>[]{type}, "interface-kind-static");
            scenario.check("valueStatic", true, Modifier.isStatic(type.getMethod("value").getModifiers()));
            scenario.check("staticValue", 21, type.getMethod("value").invoke(null));
            // The implementing class no longer inherits this default, so an
            // old invokevirtual naming Impl cannot find it. invokeinterface
            // naming Contract resolves the static member and throws ICCE.
            scenario.expectError("oldVirtual", NoSuchMethodError.class,
                    () -> InterfaceInvokeKindFixture.Caller.call(impl));
            scenario.expectError("oldInterface", IncompatibleClassChangeError.class,
                    () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
            System.gc();
            scenario.expectError("oldVirtualAfterGc", NoSuchMethodError.class,
                    () -> InterfaceInvokeKindFixture.Caller.call(impl));
            scenario.expectError("oldInterfaceAfterGc", IncompatibleClassChangeError.class,
                    () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
        });
        runScenario("interface-kind-static", 0, scenario -> {
            scenario.redefine(new Class<?>[]{type, InterfaceInvokeKindFixture.Caller.class},
                    "interface-kind-static", "interface-kind-static");
            scenario.check("valueStatic", true, Modifier.isStatic(type.getMethod("value").getModifiers()));
            scenario.check("staticValue", 21, type.getMethod("value").invoke(null));
            scenario.check("caller", 22, InterfaceInvokeKindFixture.Caller.call(impl));
            scenario.expectError("retiredDefault", NoSuchMethodError.class, () -> cachedValue.invoke(impl));
            scenario.check("twiceDefault", true, type.getMethod("twice", int.class).isDefault());
            scenario.check("twice", 12, type.getMethod("twice", int.class).invoke(impl, 4));
            scenario.check("helperPrivate", true, Modifier.isPrivate(type.getDeclaredMethod("helper").getModifiers()));
            scenario.check("useHelper", 7, type.getMethod("useHelper").invoke(impl));
            scenario.check("newImpl", 12, type.getMethod("twice", int.class).invoke(new InterfaceInvokeKindFixture.Impl(), 4));
            scenario.check("marker", 9, impl.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("callerAfterGc", 22, InterfaceInvokeKindFixture.Caller.call(impl));
        });
        for (int i = 0; i < 30000; i++) {
            if (InterfaceInvokeKindFixture.Caller.call(impl) != 22 ||
                    InterfaceInvokeKindFixture.Caller.interfaceCall(impl) != 21)
                throw new AssertionError("interface static warmup");
        }
        runScenario("interface-kind-instance-legacy", 0, scenario -> {
            scenario.redefine(new Class<?>[]{type}, "interface-kind-static-restore");
            scenario.check("valueDefault", true, type.getMethod("value").isDefault());
            scenario.check("newDefault", 13, type.getMethod("value").invoke(impl));
            for (int i = 0; i < 32; i++) {
                scenario.expectError("oldStatic" + i, IncompatibleClassChangeError.class,
                        () -> InterfaceInvokeKindFixture.Caller.call(impl));
                scenario.expectError("oldStaticDirect" + i, IncompatibleClassChangeError.class,
                        () -> InterfaceInvokeKindFixture.Caller.interfaceCall(impl));
            }
            System.gc();
            scenario.expectError("oldStaticAfterGc", IncompatibleClassChangeError.class,
                    () -> InterfaceInvokeKindFixture.Caller.call(impl));
            scenario.check("marker", 9, impl.marker);
        });
        runScenario("interface-kind-static-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{type, InterfaceInvokeKindFixture.Caller.class},
                    "interface-kind-static-restore", "interface-kind-static-restore");
            scenario.check("valueDefault", true, type.getMethod("value").isDefault());
            scenario.check("caller", 13, InterfaceInvokeKindFixture.Caller.call(impl));
            scenario.check("twiceStatic", 8, type.getMethod("twice", int.class).invoke(null, 4));
            scenario.check("useHelper", 7, type.getMethod("useHelper").invoke(impl));
            scenario.check("marker", 9, impl.marker);
        });
    }

    // Interface class modifiers evolve like a class's.
    private void runInterfaceAccessScenarios() throws Exception {
        ModifierFlagsFixture.Implementation implementation = new ModifierFlagsFixture.Implementation();
        ModifierFlagsFixture.Visible visible = implementation;
        Method value = ModifierFlagsFixture.Visible.class.getMethod("value");
        runScenario("interface-access", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ModifierFlagsFixture.Visible.class}, "interface-access");
            scenario.check("publicAfter", false, Modifier.isPublic(ModifierFlagsFixture.Visible.class.getModifiers()));
            value.setAccessible(true);
            scenario.check("value", 2, value.invoke(implementation));
            scenario.check("marker", 3, implementation.marker);
        });
        runScenario("interface-access-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{ModifierFlagsFixture.Visible.class}, "interface-access-restore");
            scenario.check("publicAfter", true, Modifier.isPublic(ModifierFlagsFixture.Visible.class.getModifiers()));
            scenario.check("value", 3, visible.value());
            scenario.check("marker", 3, implementation.marker);
        });
    }

    // An interface may newly extend an unchanged interface.
    private void runInterfaceParentScenarios() throws Exception {
        InterfaceParentFixture.Implementation implementation = new InterfaceParentFixture.Implementation();
        InterfaceParentFixture.Complete complete = new InterfaceParentFixture.Complete();
        InterfaceParentFixture.Contract contract = implementation;
        Object plain = new InterfaceParentFixture.Plain();
        implementation.marker = 9;
        runScenario("interface-parent-missing", 0, scenario -> {
            // Implementation inherits Parent.parentRequired() without a body.
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Contract.class}, "interface-parent-missing");
            scenario.check("contractExtends", true, InterfaceParentFixture.Parent.class.isAssignableFrom(InterfaceParentFixture.Contract.class));
            InterfaceParentFixture.Parent parent = (InterfaceParentFixture.Parent) (Object) implementation;
            scenario.expectError("abstractCall", AbstractMethodError.class, () -> parent.parentRequired());
            scenario.check("parentDefault", 21, parent.parentValue());
            scenario.check("completeRequired", 31, ((InterfaceParentFixture.Parent) (Object) complete).parentRequired());
            scenario.check("value", 12, contract.value());
            scenario.check("marker", 9, implementation.marker);
        });
        runScenario("interface-parent-add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Implementation.class, InterfaceParentFixture.Contract.class},
                    "interface-parent-add", "interface-parent-add");
            scenario.check("contractExtends", true, InterfaceParentFixture.Parent.class.isAssignableFrom(InterfaceParentFixture.Contract.class));
            scenario.check("instanceIsParent", true, ((Object) implementation) instanceof InterfaceParentFixture.Parent);
            InterfaceParentFixture.Parent parent = (InterfaceParentFixture.Parent) (Object) implementation;
            scenario.check("parentRequired", 41, parent.parentRequired());
            scenario.check("parentDefault", 21, parent.parentValue());
            InterfaceParentFixture.Parent completeParent = (InterfaceParentFixture.Parent) (Object) complete;
            scenario.check("completeRequired", 31, completeParent.parentRequired());
            scenario.check("completeDefault", 21, completeParent.parentValue());
            scenario.check("newRequired", 41,
                    ((InterfaceParentFixture.Parent) (Object) new InterfaceParentFixture.Implementation()).parentRequired());
            scenario.check("value", 12, contract.value());
            scenario.check("marker", 9, implementation.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("parentAfterGc", 41, parent.parentRequired());
        });
        String parentedSchema = memberSchema(InterfaceParentFixture.Contract.class);
        // Dropping the parent narrows Implementation; adding it back restores
        // the state the following scenarios start from.
        runScenario("interface-parent-remove", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Contract.class}, "interface-parent-original");
            scenario.check("notParent", false, InterfaceParentFixture.Parent.class.isAssignableFrom(InterfaceParentFixture.Contract.class));
            scenario.check("instanceIsParent", false, ((Object) implementation) instanceof InterfaceParentFixture.Parent);
            scenario.expectError("cast", ClassCastException.class,
                    () -> ((InterfaceParentFixture.Parent) (Object) implementation).parentValue());
            scenario.check("value", 11, contract.value());
            scenario.check("marker", 9, implementation.marker);
        });
        runScenario("interface-parent-readd", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Implementation.class, InterfaceParentFixture.Contract.class},
                    "interface-parent-add", "interface-parent-add");
            scenario.check("contractExtends", true, InterfaceParentFixture.Parent.class.isAssignableFrom(InterfaceParentFixture.Contract.class));
            scenario.check("instanceIsParent", true, ((Object) implementation) instanceof InterfaceParentFixture.Parent);
            scenario.check("parentRequired", 41, ((InterfaceParentFixture.Parent) (Object) implementation).parentRequired());
            scenario.check("unchangedMembers", parentedSchema, memberSchema(InterfaceParentFixture.Contract.class));
            scenario.check("value", 12, contract.value());
        });
        runScenario("interface-parent-batch", 0, scenario -> {
            // Plain links the rebuilt Parent, whose rebuild is allocated first.
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Parent.class, InterfaceParentFixture.Plain.class},
                    "interface-parent-batch", "interface-parent-batch");
            scenario.check("plainIsParent", true, plain instanceof InterfaceParentFixture.Parent);
            InterfaceParentFixture.Parent plainParent = (InterfaceParentFixture.Parent) plain;
            scenario.check("plainRequired", 51, plainParent.parentRequired());
            scenario.check("plainDefault", 22, plainParent.parentValue());
            scenario.check("implementationDefault", 22,
                    ((InterfaceParentFixture.Parent) (Object) implementation).parentValue());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("plainDefaultAfterGc", 22, plainParent.parentValue());
        });
        Object extra = Class.forName("com.github.ytlog.artdcevm.fixture.InterfaceParentFixture$Extra");
        runScenario("interface-parent-rebuilt", 0, scenario -> {
            // Contract newly extends Extra while the batch rebuilds Extra.
            scenario.redefine(new Class<?>[]{InterfaceParentFixture.Contract.class, InterfaceParentFixture.Extra.class},
                    "interface-parent-rebuilt", "interface-parent-rebuilt");
            scenario.check("extraLoaded", true, extra != null);
            scenario.check("instanceIsExtra", true, ((Object) implementation) instanceof InterfaceParentFixture.Extra);
            scenario.check("extraDefault", 32, ((InterfaceParentFixture.Extra) (Object) implementation).extraValue());
            scenario.check("value", 13, contract.value());
            scenario.check("marker", 9, implementation.marker);
        });
    }

    // A loaded subclass owns a copied interface default until its superclass
    // gains a public implementation; the re-linked subclass then has no copy.
    private void runCopiedDefaultScenario() throws Exception {
        CopiedChild child = new CopiedChild();
        CopiedDefault viaInterface = child;
        runScenario("interface-default-body", 0, scenario -> {
            scenario.redefine(new Class<?>[]{CopiedDefault.class}, "interface-default-body");
            scenario.check("interfaceCapability", true,
                    DcevmRuntime.capabilities().getBoolean("interfaceRedefinition"));
            scenario.check("oldChildInterface", 93, viaInterface.copiedValue());
            scenario.check("oldChildVirtual", 93, child.copiedValue());
            scenario.check("newChild", 93, new CopiedChild().copiedValue());
            scenario.check("parentValue", 90, child.parentValue());
        });
        if (arguments != null && "interface-default-body".equals(arguments.getString("scenario"))) return;
        runScenario("copied-default-add", 0, scenario -> {
            // The preceding interface transaction changed the copied body to 93.
            scenario.check("copiedBefore", 93, viaInterface.copiedValue());
            scenario.redefine(new Class<?>[]{CopiedParent.class}, "copied-default-add");
            scenario.check("oldChildInterface", 92, viaInterface.copiedValue());
            scenario.check("oldChildVirtual", 92, child.copiedValue());
            scenario.check("newChild", 92, new CopiedChild().copiedValue());
            scenario.check("parentValue", 90, child.parentValue());
        });
    }

    // Each case uses a fresh loader with identical binary names, so one case's
    // redefinitions cannot leak into another's loader.
    private final class AbstractGroup {
        final ClassLoader loader;
        final Class<?> contract;
        AbstractGroup() throws Exception {
            String[] names = {"Contract", "Implementation", "Other", "Existing", "Parent", "Inherited",
                    "ChildContract", "DefaultImplementation", "AbstractImplementation", "Wrong", "Caller",
                    "Generic", "Bridge", "InheritedContract", "Dependent", "Left", "Right", "Conflicting",
                    "DistantDefault", "Reabstract", "ReabstractImplementation", "DefaultCaller", "StableCaller",
                    "Removable", "LateOne", "LateTwo", "LateThree", "BatchCaller"};
            java.nio.ByteBuffer[] dexes = new java.nio.ByteBuffer[names.length];
            for (int i = 0; i < names.length; ++i)
                dexes[i] = java.nio.ByteBuffer.wrap(readAsset("interface-abstract-original/"
                        + "com/github/ytlog/artdcevm/fixture/abstractaddition/" + names[i] + ".dex"));
            loader = new dalvik.system.InMemoryDexClassLoader(dexes, context.getClassLoader());
            contract = load("Contract");
        }
        Class<?> load(String name) throws Exception {
            return Class.forName("com.github.ytlog.artdcevm.fixture.abstractaddition." + name, false, loader);
        }
        Object instance(String name) throws Exception { return load(name).getConstructor().newInstance(); }
        Object call(Object receiver) throws Exception {
            return load("Caller").getMethod("call", contract).invoke(null, receiver);
        }
        // As on JBR, a class without the new abstract method links one that
        // throws AbstractMethodError at the call.
        void unimplemented(Scenario scenario, String name, Object receiver) throws Exception {
            scenario.expectError(name, AbstractMethodError.class, () -> call(receiver));
        }
    }

    private void runAbstractInterfaceScenarios() throws Exception {
        for (boolean reverse : new boolean[]{false, true}) {
            AbstractGroup group = new AbstractGroup();
            Class<?> implementation = group.load("Implementation");
            Object instance = implementation.getField("singleton").get(null);
            implementation.getField("marker").setInt(instance, 73);
            implementation.getField("counter").setInt(null, 79);
            // Keep an abstract implementor in the group.
            group.load("AbstractImplementation");
            AbstractWitness witness = new AbstractWitness(group, instance, 0);
            Class<?> batchCaller = group.load("BatchCaller");
            Method cachedCaller = batchCaller.getMethod("call", group.contract);
            if (!"old/11".equals(cachedCaller.invoke(null, instance))) throw new AssertionError("Old caller failed");
            runScenario("interface-abstract-batch-" + (reverse ? "reverse" : "forward"), 0, scenario -> {
                Class<?>[] batch = reverse ? new Class<?>[]{batchCaller, implementation, group.contract}
                        : new Class<?>[]{group.contract, implementation, batchCaller};
                scenario.redefine(batch, "interface-abstract-add", "interface-abstract-add", "interface-abstract-add");
                scenario.check("sameBatchCaller", "new/12", cachedCaller.invoke(null, instance));
                scenario.check("newCaller", "new", group.call(instance));
                scenario.check("singleton", true, instance == implementation.getField("singleton").get(null));
                scenario.check("marker", 73, implementation.getField("marker").getInt(instance));
                scenario.check("counter", 79, implementation.getField("counter").getInt(null));
                scenario.check("bodyUpdated", "new", implementation.getMethod("version").invoke(instance));
                witness.check(scenario, "cached", 12);
                System.gc(); System.runFinalization(); System.gc();
                scenario.check("afterGc", "new", group.call(instance));
            });
        }
        for (String type : new String[]{"Existing", "Inherited", "DefaultImplementation", "Bridge"}) {
            AbstractGroup group = new AbstractGroup();
            Object instance = group.instance(type);
            String expected = type.equals("Existing") ? "existing" : type.equals("Inherited") ? "parent"
                    : type.equals("Bridge") ? "bridge" : "default";
            String variant = type.equals("Bridge") ? "interface-abstract-bridge" : "interface-abstract-add";
            runScenario("interface-abstract-" + type.toLowerCase(java.util.Locale.ROOT), 0, scenario -> {
                scenario.redefine(new Class<?>[]{group.contract}, variant);
                // The caller has a String return descriptor; Object bridge
                // dispatch uses the interface Method on the bridge variant.
                scenario.check("implementation", expected, type.equals("Bridge")
                        ? group.contract.getMethod("required").invoke(instance) : group.call(instance));
                scenario.check("interfaceBody", 12, group.contract.getMethod("oldValue").invoke(instance));
            });
        }
        AbstractGroup missing = new AbstractGroup();
        Object unchanged = missing.instance("Implementation");
        runScenario("interface-abstract-missing", 0, scenario -> {
            scenario.redefine(new Class<?>[]{missing.contract}, "interface-abstract-add");
            missing.unimplemented(scenario, "abstractCall", unchanged);
            scenario.check("newBody", 12, missing.contract.getMethod("oldValue").invoke(unchanged));
            scenario.check("declared", "required", missing.contract.getMethod("required").getName());
        });
        runScenario("interface-abstract-withdraw", 0, scenario -> {
            scenario.redefine(new Class<?>[]{missing.contract}, "interface-abstract-original");
            scenario.check("oldBody", 11, missing.contract.getMethod("oldValue").invoke(unchanged));
        });
        runScenario("interface-abstract-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{missing.contract, missing.load("Implementation")},
                    "interface-abstract-add", "interface-abstract-add");
            scenario.check("newCaller", "new", missing.call(unchanged));
        });
        AbstractGroup partial = new AbstractGroup();
        Object filled = partial.instance("Implementation");
        // Other is loaded without instances while the batch adds the method.
        partial.load("Other");
        runScenario("interface-abstract-partial", 0, scenario -> {
            scenario.redefine(new Class<?>[]{partial.contract, partial.load("Implementation")},
                    "interface-abstract-add", "interface-abstract-add");
            scenario.check("newCaller", "new", partial.call(filled));
            scenario.check("implementationUpdated", "new", partial.load("Implementation").getMethod("version").invoke(filled));
            partial.unimplemented(scenario, "otherAbstractCall", partial.instance("Other"));
        });
        runScenario("interface-abstract-partial-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{partial.load("Other"), partial.load("Implementation"), partial.contract},
                    "interface-abstract-add", "interface-abstract-add", "interface-abstract-add");
            scenario.check("newCaller", "new", partial.call(filled));
            scenario.check("other", "other", partial.call(partial.instance("Other")));
        });
        for (String kind : new String[]{"signature", "wrong-return", "private", "package", "protected", "static", "bridge-missing", "conflict"}) {
            runAbstractUnimplementedScenario(kind);
        }
        AbstractGroup dependent = new AbstractGroup();
        Object receiver = dependent.instance("Dependent");
        Object sibling = dependent.instance("Existing");
        Class.forName(dependent.load("InheritedContract").getName(), true, dependent.loader);
        runScenario("interface-abstract-dependent-missing", 0, scenario -> {
            scenario.redefine(new Class<?>[]{dependent.load("InheritedContract"), dependent.contract},
                    "interface-abstract-dependent", "interface-abstract-dependent");
            scenario.expectError("abstractCall", AbstractMethodError.class,
                    () -> dependent.load("InheritedContract").getMethod("required").invoke(receiver));
            scenario.check("newBody", 12, dependent.contract.getMethod("oldValue").invoke(receiver));
        });
        runScenario("interface-abstract-dependent-owner", 0, scenario -> {
            scenario.redefine(new Class<?>[]{dependent.load("InheritedContract"), dependent.load("Dependent"), dependent.contract},
                    "interface-abstract-dependent", "interface-abstract-dependent", "interface-abstract-dependent");
            scenario.check("dependent", "dependent", dependent.load("InheritedContract").getMethod("required").invoke(receiver));
            scenario.check("ownerBody", 12, dependent.contract.getMethod("oldValue").invoke(receiver));
            scenario.check("siblingBody", 12, dependent.contract.getMethod("oldValue").invoke(sibling));
        });
        // Same binary names in two simultaneously loaded groups: only the
        // transaction's defining loader takes the new declaration.
        AbstractGroup isolated = new AbstractGroup();
        Object isolatedOld = isolated.instance("Implementation");
        AbstractGroup accepted = new AbstractGroup();
        Object implemented = accepted.instance("Existing");
        runScenario("interface-abstract-loader-identity", 0, scenario -> {
            scenario.redefine(new Class<?>[]{accepted.contract}, "interface-abstract-add");
            scenario.check("newCaller", "existing", accepted.call(implemented));
            scenario.check("otherLoaderOldBody", 11, isolated.contract.getMethod("oldValue").invoke(isolatedOld));
            scenario.expectError("otherLoaderNoDeclaration", NoSuchMethodException.class, () -> isolated.contract.getMethod("required"));
        });
        runAbstractBoundaryScenarios();
    }

    private static Object invokeCachedHandle(MethodHandle handle, Object receiver) throws Exception {
        try { return handle.invokeWithArguments(receiver); }
        catch (Throwable error) {
            if (error instanceof Exception) throw (Exception) error;
            if (error instanceof Error) throw (Error) error;
            throw new RuntimeException(error);
        }
    }

    private final class AbstractWitness {
        final Object receiver;
        final Method reflected, bytecode;
        final MethodHandle handle;
        final int slot;
        AbstractWitness(AbstractGroup group, Object receiver, int slot) throws Exception {
            this.receiver = receiver; this.slot = slot;
            reflected = group.contract.getMethod("oldValue");
            handle = MethodHandles.lookup().unreflect(reflected);
            Class<?> caller = group.load("StableCaller");
            bytecode = caller.getMethod("call", group.contract);
            if (!Integer.valueOf(11).equals(caller.getMethod("warm", group.contract).invoke(null, receiver)))
                throw new AssertionError("Old invoke-interface warmup failed");
            StandardJvmtiClient.cacheInterfaceDispatch(slot, reflected);
        }
        void check(Scenario scenario, String prefix, int expected) throws Exception {
            scenario.check(prefix + "Reflection", expected, reflected.invoke(receiver));
            scenario.check(prefix + "Handle", expected, invokeCachedHandle(handle, receiver));
            scenario.check(prefix + "Bytecode", expected, bytecode.invoke(null, receiver));
            scenario.check(prefix + "Jni", expected, StandardJvmtiClient.callInterfaceDispatchInt(receiver, slot));
        }
    }

    private void runAbstractBoundaryScenarios() throws Exception {
        AbstractGroup reabstract = new AbstractGroup();
        Object descendant = reabstract.instance("ReabstractImplementation");
        Class<?> narrow = reabstract.load("Reabstract");
        Class.forName(narrow.getName(), true, reabstract.loader);
        Method oldDefault = reabstract.load("DistantDefault").getMethod("required");
        Method oldCaller = reabstract.load("DefaultCaller").getMethod("call", reabstract.load("DistantDefault"));
        reabstract.load("DefaultCaller").getMethod("warm", reabstract.load("DistantDefault")).invoke(null, descendant);
        StandardJvmtiClient.cacheInterfaceDispatch(0, oldDefault);
        // The new abstract redeclaration hides the distant default from the
        // implementation, which lacks the method: every path throws.
        runScenario("interface-abstract-reabstract-missing", 0, scenario -> {
            scenario.redefine(new Class<?>[]{narrow}, "interface-abstract-reabstract");
            scenario.expectError("cachedDefaultCall", AbstractMethodError.class, () -> oldCaller.invoke(null, descendant));
            scenario.expectError("cachedDefaultReflection", AbstractMethodError.class, () -> oldDefault.invoke(descendant));
            scenario.expectError("cachedDefaultJni", AbstractMethodError.class,
                    () -> StandardJvmtiClient.callInterfaceDispatchString(descendant, 0));
        });
        runScenario("interface-abstract-reabstract-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{narrow, reabstract.load("ReabstractImplementation")},
                    "interface-abstract-reabstract", "interface-abstract-reabstract");
            scenario.check("newImplementation", "implemented", narrow.getMethod("required").invoke(descendant));
            scenario.check("cachedDefaultCall", "implemented", oldCaller.invoke(null, descendant));
            scenario.check("cachedDefaultReflection", "implemented", oldDefault.invoke(descendant));
            scenario.check("cachedDefaultJni", "implemented", StandardJvmtiClient.callInterfaceDispatchString(descendant, 0));
        });

        AbstractGroup remove = new AbstractGroup();
        Object removable = remove.instance("Removable");
        Method implementation = remove.load("Removable").getMethod("required");
        StandardJvmtiClient.cacheInterfaceDispatch(0, implementation);
        runScenario("interface-abstract-remove-last", 0, scenario -> {
            scenario.redefine(new Class<?>[]{remove.contract, remove.load("Removable")},
                    "interface-abstract-remove", "interface-abstract-remove");
            remove.unimplemented(scenario, "abstractCall", removable);
            scenario.expectReflectionError("retiredReflection", () -> implementation.invoke(removable));
        });
        runScenario("interface-abstract-remove-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{remove.contract, remove.load("Removable")},
                    "interface-abstract-add", "interface-abstract-original");
            scenario.check("newCaller", "removable", remove.call(removable));
        });

        AbstractGroup good = new AbstractGroup(), bad = new AbstractGroup();
        Object goodInstance = good.instance("Existing"), badInstance = bad.instance("Implementation");
        AbstractWitness first = new AbstractWitness(good, goodInstance, 0);
        AbstractWitness second = new AbstractWitness(bad, badInstance, 1);
        runScenario("interface-abstract-multi-group-missing", 0, scenario -> {
            scenario.redefine(new Class<?>[]{good.contract, bad.contract}, "interface-abstract-add", "interface-abstract-add");
            first.check(scenario, "first", 12); second.check(scenario, "second", 12);
            scenario.check("firstNewCaller", "existing", good.call(goodInstance));
            bad.unimplemented(scenario, "secondAbstractCall", badInstance);
        });
        runScenario("interface-abstract-multi-group-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{good.contract, bad.contract, bad.load("Implementation")},
                    "interface-abstract-add", "interface-abstract-add", "interface-abstract-add");
            first.check(scenario, "first", 12); second.check(scenario, "second", 12);
            scenario.check("firstNewCaller", "existing", good.call(goodInstance));
            scenario.check("secondNewCaller", "new", bad.call(badInstance));
        });

        AbstractGroup planned = new AbstractGroup();
        Object plannedInstance = planned.instance("Implementation");
        AbstractWitness planWitness = new AbstractWitness(planned, plannedInstance, 0);
        runScenario("interface-abstract-after-plan-failure", 110, scenario -> {
            scenario.check("armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
            scenario.redefine(new Class<?>[]{planned.contract, planned.load("Implementation")},
                    "interface-abstract-add", "interface-abstract-add");
            scenario.check("planFailure", true, DcevmRuntime.capabilities().getString("lastRejection").contains("after interface mapping plan"));
            planWitness.check(scenario, "old", 11);
            scenario.check("oldImplementation", "old", planned.load("Implementation").getMethod("version").invoke(plannedInstance));
        });
        runScenario("interface-abstract-after-plan-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{planned.contract, planned.load("Implementation")},
                    "interface-abstract-add", "interface-abstract-add");
            planWitness.check(scenario, "new", 12);
            scenario.check("newCaller", "new", planned.call(plannedInstance));
        });

        AbstractGroup late = new AbstractGroup();
        Object existing = late.instance("Existing");
        AbstractWitness lateWitness = new AbstractWitness(late, existing, 0);
        long retries = DcevmRuntime.capabilities().getLong("subclassLoadRetries");
        runScenario("interface-abstract-late-missing", 0, scenario -> {
            scenario.check("hook", 0, StandardJvmtiClient.setClassFileLoads(late.contract.getName().replace('.', '/'),
                    late.loader, new String[]{"com.github.ytlog.artdcevm.fixture.abstractaddition.Other"}));
            try { scenario.redefine(new Class<?>[]{late.contract}, "interface-abstract-add"); }
            finally { StandardJvmtiClient.setClassFileLoads(null, null, null); }
            scenario.check("retried", 1L, DcevmRuntime.capabilities().getLong("subclassLoadRetries") - retries);
            lateWitness.check(scenario, "new", 12);
            late.unimplemented(scenario, "otherAbstractCall", late.instance("Other"));
        });
        runScenario("interface-abstract-late-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{late.contract, late.load("Other")}, "interface-abstract-add", "interface-abstract-add");
            lateWitness.check(scenario, "new", 12);
            scenario.check("existing", "existing", late.call(existing));
            scenario.check("other", "other", late.call(late.instance("Other")));
        });

        AbstractGroup exhausted = new AbstractGroup();
        Object exhaustedInstance = exhausted.instance("Existing");
        AbstractWitness exhaustedWitness = new AbstractWitness(exhausted, exhaustedInstance, 0);
        long beforeExhaustion = DcevmRuntime.capabilities().getLong("subclassLoadRetries");
        runScenario("interface-abstract-retries-exhausted", 113, scenario -> {
            String base = "com.github.ytlog.artdcevm.fixture.abstractaddition.";
            scenario.check("hook", 0, StandardJvmtiClient.setClassFileLoads(exhausted.contract.getName().replace('.', '/'),
                    exhausted.loader, new String[]{base + "LateOne", base + "LateTwo", base + "LateThree"}));
            try { scenario.redefine(new Class<?>[]{exhausted.contract}, "interface-abstract-add"); }
            finally { StandardJvmtiClient.setClassFileLoads(null, null, null); }
            scenario.check("boundedRetries", 2L, DcevmRuntime.capabilities().getLong("subclassLoadRetries") - beforeExhaustion);
            scenario.check("reason", true, DcevmRuntime.capabilities().getString("lastRejection").contains("loaded during the transaction"));
            exhaustedWitness.check(scenario, "old", 11);
        });
        runScenario("interface-abstract-exhausted-withdraw", 0, scenario -> {
            scenario.redefine(new Class<?>[]{exhausted.contract}, "interface-abstract-original");
            exhaustedWitness.check(scenario, "old", 11);
        });
        runScenario("interface-abstract-exhausted-retry", 0, scenario -> {
            scenario.redefine(new Class<?>[]{exhausted.contract, exhausted.load("LateOne"), exhausted.load("LateTwo"), exhausted.load("LateThree")},
                    "interface-abstract-add", "interface-abstract-add", "interface-abstract-add", "interface-abstract-add");
            exhaustedWitness.check(scenario, "new", 12);
            scenario.check("existing", "existing", exhausted.call(exhaustedInstance));
            scenario.check("lateOne", "late-one", exhausted.call(exhausted.instance("LateOne")));
        });
    }

    // Up to Android 14, ART fails to link a package-private or protected
    // implementation of an interface method with IllegalAccessError, so the
    // whole transaction fails; later releases link the class and the call
    // throws IllegalAccessError, also for a private method. JBR links it and
    // throws at the call (AbstractMethodError for a private method).
    static final int NON_PUBLIC_LINK_ERROR_API = 34;

    static int abstractUnimplementedCode(String kind) {
        boolean nonPublic = kind.equals("package") || kind.equals("protected");
        return nonPublic && Build.VERSION.SDK_INT <= NON_PUBLIC_LINK_ERROR_API ? 63 : 0;
    }

    // The interface gains an abstract method that the class does not implement
    // with a public instance method of the same descriptor.
    private void runAbstractUnimplementedScenario(String kind) throws Exception {
        if (!java.util.Arrays.asList("signature", "wrong-return", "private", "package", "protected",
                "static", "bridge-missing", "conflict").contains(kind))
            throw new IllegalArgumentException("Unknown abstract implementation scenario");
        AbstractGroup group = new AbstractGroup();
        String type = kind.equals("signature") || kind.equals("wrong-return") ? "Wrong"
                : kind.equals("bridge-missing") ? "Bridge" : kind.equals("conflict") ? "Conflicting" : "Implementation";
        Object old = group.instance(type);
        String variant = "interface-abstract-" + kind;
        boolean contractOnly = kind.equals("signature") || kind.equals("conflict");
        int code = abstractUnimplementedCode(kind);
        runScenario("interface-abstract-unimplemented-" + kind, code, scenario -> {
            if (contractOnly) scenario.redefine(new Class<?>[]{group.contract}, "interface-abstract-add");
            else scenario.redefine(new Class<?>[]{group.contract, group.load(type)},
                    kind.equals("bridge-missing") ? variant : "interface-abstract-add", variant);
            if (code != 0) {
                String reason = DcevmRuntime.capabilities().getString("lastRejection");
                scenario.check("classNamed", true, reason.contains(type + ";"));
                scenario.check("linkError", true, reason.contains("as IllegalAccessError"));
                scenario.check("oldBody", 11, group.contract.getMethod("oldValue").invoke(old));
                scenario.expectError("noDeclaration", NoSuchMethodException.class, () -> group.contract.getMethod("required"));
                return;
            }
            scenario.check("newBody", 12, group.contract.getMethod("oldValue").invoke(old));
            // Two unrelated defaults stay a conflict; otherwise nothing implements the method.
            boolean nonPublic = kind.equals("package") || kind.equals("protected")
                    || (kind.equals("private") && Build.VERSION.SDK_INT > NON_PUBLIC_LINK_ERROR_API);
            Class<? extends Throwable> expected = kind.equals("conflict") ? IncompatibleClassChangeError.class
                    : nonPublic ? IllegalAccessError.class : AbstractMethodError.class;
            scenario.expectError("interfaceCall", expected, () -> group.contract.getMethod("required").invoke(old));
        });
        if (type.equals("Implementation")) {
            runScenario("interface-abstract-retry-" + kind, 0, scenario -> {
                scenario.redefine(new Class<?>[]{group.contract, group.load(type)},
                        "interface-abstract-add", "interface-abstract-add");
                scenario.check("newCaller", "new", group.call(old));
            });
        }
    }

    private void runInterfaceShadowScenarios() throws Exception {
        // Static/private shadows preserve JBR resolution. Non-public virtual
        // implementations and cross-package package members cannot preserve
        // JBR dispatch when ART copies a new default; reject before commit.
        Object[] shadows = {new InterfaceEvolutionFixture.StaticImpl(), new InterfaceEvolutionFixture.PrivateImpl(),
                new InterfaceEvolutionFixture.ProtectedImpl(), new InterfaceEvolutionFixture.PackageImpl(),
                new InterfaceEvolutionFixture.OtherPackageImpl()};
        Class<?>[] contracts = {InterfaceEvolutionFixture.StaticShadow.class, InterfaceEvolutionFixture.PrivateShadow.class,
                InterfaceEvolutionFixture.ProtectedShadow.class, InterfaceEvolutionFixture.PackageShadow.class,
                InterfaceEvolutionFixture.OtherPackageShadow.class};
        String[] kinds = {"static", "private", "protected", "package", "other-package"};
        for (int i = 0; i < kinds.length; ++i) {
            final int index = i;
            String name = "interface-shadow-" + kinds[i];
            Class<?> type = contracts[i]; Object receiver = shadows[i];
            Method stable = type.getMethod("stable");
            String schema = memberSchema(type);
            int code = index >= 2 ? 63 : 0;
            runScenario(name, code, scenario -> {
                scenario.redefine(new Class<?>[]{type}, name);
                int old = 51 + 10 * index;
                if (code != 0) {
                    String reason = DcevmRuntime.capabilities().getString("lastRejection");
                    String owner = index == 2 ? "ProtectedBase" : index == 3 ? "PackageBase" : "OtherPackageBase";
                    scenario.check("rejectionReason", true, reason.contains(owner + ";: shadow()I"));
                    scenario.check("unchangedMembers", schema, memberSchema(type));
                } else {
                    old = 0;
                    scenario.check("interfaceShadow", 1, type.getMethod("shadow").invoke(receiver));
                }
                scenario.check("oldDispatch", old, stable.invoke(receiver));
                Object fresh = receiver.getClass().getConstructor().newInstance();
                scenario.check("newDispatch", old, stable.invoke(fresh));
                scenario.check("oldShadow", 52 + 10 * index, index == 0
                        ? InterfaceEvolutionFixture.StaticBase.shadow()
                        : receiver.getClass().getMethod("oldCall").invoke(receiver));
                System.gc(); System.runFinalization(); System.gc();
                scenario.check("afterGc", old, stable.invoke(receiver));
            });
        }
    }

    private void runInterfaceEvolutionScenarios() throws Exception {
        InterfaceEvolutionFixture.Implementation instance = new InterfaceEvolutionFixture.Implementation();
        instance.marker = 73;
        InterfaceDirectTargets signature = new InterfaceDirectTargets(InterfaceEvolutionFixture.Contract.class, instance);
        runScenario("interface-members-add", 0, scenario -> {
            signature.checkLive(scenario);
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Contract.class}, "interface-members-add");
            scenario.check("oldAdded", 110, InterfaceEvolutionFixture.Contract.class.getMethod("added").invoke(instance));
            scenario.check("newAdded", 110, InterfaceEvolutionFixture.Contract.class.getMethod("added")
                    .invoke(new InterfaceEvolutionFixture.Implementation()));
            scenario.check("oldDispatch", 63, instance.virtualValue());
            scenario.check("marker", 73, instance.marker);
            String[] methods = {"staticAdded", "publicAdded", "packageAdded", "protectedAdded", "syntheticAdded"};
            int[] values = {10, 12, 13, 14, 15};
            int[] flags = {10, 9, 8, 12, 0x1009};
            for (int i = 0; i < methods.length; ++i) {
                Method method = InterfaceEvolutionFixture.Contract.class.getDeclaredMethod(methods[i]);
                method.setAccessible(true);
                scenario.check(methods[i], values[i], method.invoke(null));
                scenario.check(methods[i] + "Flags", flags[i], method.getModifiers());
            }
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 110, InterfaceEvolutionFixture.Contract.class.getMethod("added").invoke(instance));
        });
        runScenario("interface-members-signature", 0, scenario -> {
            signature.checkLive(scenario);
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Contract.class}, "interface-members-signature");
            signature.checkRetired(scenario, "");
            scenario.expectError("bytecodeDirect", NoSuchMethodError.class, instance::throughDirect);
            scenario.expectError("bytecodeStatic", NoSuchMethodError.class, instance::throughStatic);
            Method direct = InterfaceEvolutionFixture.Contract.class.getDeclaredMethod("directValue", int.class);
            direct.setAccessible(true);
            scenario.check("newDirect", 161, direct.invoke(instance, 1));
            scenario.check("newStatic", 182, InterfaceEvolutionFixture.Contract.class
                    .getDeclaredMethod("staticValue", int.class).invoke(null, 2));
            scenario.check("oldDispatch", 63, instance.virtualValue());
            scenario.check("marker", 73, instance.marker);
            System.gc(); System.runFinalization(); System.gc();
            signature.checkRetired(scenario, "AfterGc");
            scenario.check("afterGc", 63, instance.virtualValue());
        });
        InterfaceEvolutionFixture.DeleteImpl deleted = new InterfaceEvolutionFixture.DeleteImpl();
        InterfaceDirectTargets deletion = new InterfaceDirectTargets(InterfaceEvolutionFixture.Deletion.class, deleted);
        runScenario("interface-members-delete", 0, scenario -> {
            deletion.checkLive(scenario);
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Deletion.class}, "interface-members-delete");
            deletion.checkRetired(scenario, "");
            scenario.expectError("bytecodeDirect", NoSuchMethodError.class, deleted::throughDirect);
            scenario.expectError("bytecodeStatic", NoSuchMethodError.class, deleted::throughStatic);
            scenario.check("oldDispatch", 63, deleted.virtualValue());
            scenario.check("newDispatch", 63, new InterfaceEvolutionFixture.DeleteImpl().virtualValue());
            System.gc(); System.runFinalization(); System.gc();
            deletion.checkRetired(scenario, "AfterGc");
            scenario.check("afterGc", 63, deleted.virtualValue());
        });
        InterfaceEvolutionFixture.RootImpl root = new InterfaceEvolutionFixture.RootImpl();
        InterfaceEvolutionFixture.ChildImpl child = new InterfaceEvolutionFixture.ChildImpl();
        InterfaceEvolutionFixture.PublicImpl publicImpl = new InterfaceEvolutionFixture.PublicImpl();
        root.marker = 79; child.marker = 83;
        runScenario("interface-default-add", 0, scenario -> {
            scenario.check("before", 20, root.inherited());
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Root.class}, "interface-default-add");
            Method added = InterfaceEvolutionFixture.Root.class.getMethod("added");
            scenario.check("copiedDefault", 60, added.invoke(root));
            scenario.check("newCopiedDefault", 60, added.invoke(new InterfaceEvolutionFixture.RootImpl()));
            scenario.check("subinterfaceWins", 70, added.invoke(child));
            scenario.check("publicClassWins", 80, added.invoke(publicImpl));
            scenario.check("overridesParent", 50, root.inherited());
            scenario.check("oldDispatch", 1, root.value());
            scenario.check("marker", 79, root.marker);
            scenario.check("childMarker", 83, child.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 60, added.invoke(root));
            scenario.check("childAfterGc", 70, added.invoke(child));
        });
        InterfaceEvolutionFixture.MirandaImpl miranda = new InterfaceEvolutionFixture.MirandaImpl();
        InterfaceEvolutionFixture.MirandaRoot viaMiranda = miranda;
        runScenario("interface-miranda-prepare", 0, scenario -> {
            scenario.check("before", 9, viaMiranda.miranda());
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.MirandaImpl.class}, "interface-miranda-prepare");
            scenario.expectError("miranda", AbstractMethodError.class, viaMiranda::miranda);
            scenario.check("stable", 7, viaMiranda.stable());
        });
        runScenario("interface-miranda-add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.MirandaRoot.class}, "interface-miranda-add");
            scenario.check("oldMiranda", 99, viaMiranda.miranda());
            scenario.check("newMiranda", 99, new InterfaceEvolutionFixture.MirandaImpl().miranda());
            scenario.check("stable", 7, viaMiranda.stable());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 99, viaMiranda.miranda());
        });
        InterfaceEvolutionFixture.Diamond diamond = new InterfaceEvolutionFixture.Diamond();
        diamond.marker = 89;
        runScenario("interface-default-conflict", 0, scenario -> {
            // Left gains a default that conflicts with Right's in Diamond: as on
            // JBR, the class links a method that throws at the call.
            scenario.check("before", 21, diamond.conflict());
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Left.class}, "interface-default-conflict");
            scenario.expectError("oldDispatch", IncompatibleClassChangeError.class, () -> diamond.conflict());
            scenario.expectError("newDispatch", IncompatibleClassChangeError.class,
                    () -> new InterfaceEvolutionFixture.Diamond().conflict());
            scenario.check("left", 12, diamond.left());
            scenario.check("marker", 89, diamond.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.expectError("afterGc", IncompatibleClassChangeError.class, () -> diamond.conflict());
        });
        InterfaceEvolutionFixture.HidingImpl hiding = new InterfaceEvolutionFixture.HidingImpl();
        for (String kind : new String[]{"static", "private"}) {
            String name = "interface-hide-parent-" + kind;
            String schema = memberSchema(InterfaceEvolutionFixture.Hiding.class);
            runScenario(name, 63, scenario -> {
                scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.Hiding.class}, name);
                scenario.check("rejectionReason", true, DcevmRuntime.capabilities().getString("lastRejection")
                        .contains("hides an inherited interface method: clash()I"));
                scenario.check("unchangedMembers", schema, memberSchema(InterfaceEvolutionFixture.Hiding.class));
                scenario.check("oldDispatch", 31, hiding.clash());
                scenario.check("newDispatch", 31, new InterfaceEvolutionFixture.HidingImpl().clash());
                System.gc(); System.runFinalization(); System.gc();
                scenario.check("afterGc", 31, hiding.clash());
            });
        }
        InterfaceEvolutionFixture.ObjectImpl object = new InterfaceEvolutionFixture.ObjectImpl();
        int oldHash = object.hashCode();
        runScenario("interface-hide-object", 63, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.ObjectHiding.class}, "interface-hide-object");
            scenario.check("rejectionReason", true, DcevmRuntime.capabilities().getString("lastRejection")
                    .contains("hides an inherited interface method: hashCode()I"));
            scenario.check("oldHash", oldHash, object.hashCode());
            scenario.check("oldDispatch", 41, object.stable());
            scenario.check("newDispatch", 41, new InterfaceEvolutionFixture.ObjectImpl().stable());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 41, object.stable());
        });
        runInterfaceShadowScenarios();
        InterfaceEvolutionFixture.BridgeImpl bridge = new InterfaceEvolutionFixture.BridgeImpl();
        InterfaceEvolutionFixture.BridgeParent<Integer> erased = bridge;
        runScenario("interface-bridge-add", 0, scenario -> {
            scenario.check("before", 90, erased.result());
            scenario.redefine(new Class<?>[]{InterfaceEvolutionFixture.BridgeImpl.class,
                    InterfaceEvolutionFixture.BridgeContract.class}, "interface-bridge-add", "interface-bridge-add");
            scenario.check("oldBridge", 90, erased.result());
            scenario.check("newBridge", 90, new InterfaceEvolutionFixture.BridgeImpl().result());
            Method added = InterfaceEvolutionFixture.BridgeContract.class.getMethod("added");
            scenario.check("oldAdded", 91, added.invoke(bridge));
            scenario.check("newAdded", 91, added.invoke(new InterfaceEvolutionFixture.BridgeImpl()));
            boolean found = false;
            for (Method method : InterfaceEvolutionFixture.BridgeImpl.class.getDeclaredMethods())
                if (method.getName().equals("result") && method.getReturnType() == Object.class && method.isBridge()) found = true;
            scenario.check("hasBridge", true, found);
            scenario.check("addedBridge", true, InterfaceEvolutionFixture.BridgeImpl.class
                    .getDeclaredMethod("added").isBridge());
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("afterGc", 91, added.invoke(bridge));
        });
        runInterfaceLambdaScenarios();
    }

    /** The same JNI call forms as ordinary retirement, against interface IDs. */
    private static final class InterfaceDirectTargets {
        final Class<?> type;
        final Object receiver;
        final Method[] methods = new Method[2];
        final MethodHandle[] handles = new MethodHandle[2];
        InterfaceDirectTargets(Class<?> type, Object receiver) throws Exception {
            this.type = type; this.receiver = receiver;
            for (int i = 0; i < 2; ++i) {
                methods[i] = type.getDeclaredMethod(i == 0 ? "directValue" : "staticValue");
                methods[i].setAccessible(true);
                handles[i] = MethodHandles.lookup().unreflect(methods[i]);
            }
            StandardJvmtiClient.cacheMethodIds(type);
        }
        void checkLive(Scenario scenario) throws Exception {
            for (int i = 0; i < 2; ++i) {
                scenario.check("reflectionBefore" + i, i == 0 ? 61 : 82, methods[i].invoke(i == 0 ? receiver : null));
                for (int form = 0; form < 3; ++form)
                    scenario.check("jniBefore" + i + form, i == 0 ? 61 : 82,
                            StandardJvmtiClient.callCachedMethodVariant(receiver, i, form));
            }
        }
        void checkRetired(Scenario scenario, String suffix) throws Exception {
            for (int i = 0; i < 2; ++i) {
                final int index = i;
                scenario.expectError("lookup" + i + suffix, NoSuchMethodException.class,
                        () -> type.getDeclaredMethod(index == 0 ? "directValue" : "staticValue"));
                scenario.expectError("jniLookup" + i + suffix, NoSuchMethodError.class,
                        () -> StandardJvmtiClient.lookupMethodId(type, index));
                scenario.expectReflectionError("reflection" + i + suffix,
                        () -> methods[index].invoke(index == 0 ? receiver : null));
                scenario.expectError("handle" + i + suffix, NoSuchMethodError.class,
                        () -> { if (index == 0) handles[index].invokeWithArguments(receiver);
                                else handles[index].invokeWithArguments(); });
                for (int form = 0; form < 3; ++form) {
                    final int callForm = form;
                    scenario.expectError("jni" + i + form + suffix, NoSuchMethodError.class,
                            () -> StandardJvmtiClient.callCachedMethodVariant(receiver, index, callForm));
                    scenario.expectError("reflectedJni" + i + form + suffix, NoSuchMethodError.class,
                            () -> StandardJvmtiClient.callReflectedMethodVariant(methods[index], receiver, index, callForm));
                }
            }
        }
    }

    private void runInterfaceLambdaScenarios() throws Exception {
        com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaImpl instance =
                new com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaImpl();
        instance.marker = 97;
        String path = "com/github/ytlog/artdcevm/fixture/interfacelambda";
        String[] kinds = {"first", "append", "insert"};
        int[] values = {5, 11, 111};
        Method[] bytecodeCaller = new Method[1];
        for (int i = 0; i < kinds.length; ++i) {
            final int index = i;
            String name = "interface-lambda-" + kinds[i];
            List<Method> retired = new ArrayList<>();
            List<MethodHandle> handles = new ArrayList<>();
            if (i == 2) {
                for (Method method : com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaContract.class.getDeclaredMethods()) {
                    if (Modifier.isStatic(method.getModifiers()) && method.getParameterTypes().length == 1) {
                        method.setAccessible(true); retired.add(method);
                        handles.add(MethodHandles.lookup().unreflect(method));
                    }
                }
                StandardJvmtiClient.cacheInterfaceStaticMethods(
                        com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaContract.class, retired.toArray(new Method[0]));
            }
            runScenario(name, 0, scenario -> {
                if (index == 2) {
                    scenario.check("bytecodeBefore", 5, bytecodeCaller[0].invoke(null, instance));
                    scenario.check("cachedStaticTargets", 4, retired.size());
                    for (int target = 0; target < retired.size(); ++target) {
                        int value = (Integer) retired.get(target).invoke(null, instance);
                        scenario.check("liveStaticTarget" + target, true, value == 5 || value == 6);
                        scenario.check("jniStaticBefore" + target, value,
                                StandardJvmtiClient.callInterfaceStatic(instance, target));
                    }
                }
                HotReloadTool tool = new HotReloadTool(context).prepare(name, path);
                scenario.result.put("classification", tool.classification());
                scenario.check("appendCode", 0, tool.appendCode);
                scenario.check("redefinedTypes", index + 1, tool.existingClasses().length);
                scenario.redefine(tool.existingClasses(), java.util.Collections.nCopies(index + 1, name).toArray(new String[0]));
                scenario.check("oldDispatch", values[index], instance.value());
                scenario.check("newDispatch", values[index],
                        new com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaImpl().value());
                scenario.check("marker", 97, instance.marker);
                if (index == 1) {
                    HotReloadTool caller = new HotReloadTool(context).prepare("interface-lambda-caller", path);
                    scenario.check("callerAppendCode", 0, caller.appendCode);
                    scenario.check("callerExistingTypes", 1, caller.existingClasses().length);
                    // The fixture's interface is only a compile-time stub. Append
                    // the new caller; keep the actual interface definition.
                    bytecodeCaller[0] = Class.forName(path.replace('/', '.') + ".LambdaRetiredCaller")
                            .getMethod("call", com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaContract.class);
                    scenario.check("bytecodeBridge", 5, bytecodeCaller[0].invoke(null, instance));
                }
                if (index == 2) scenario.expectReflectionError("bytecodeBridge",
                        () -> bytecodeCaller[0].invoke(null, instance));
                int removed = 0;
                for (int j = 0; j < retired.size(); ++j) {
                    Method method = retired.get(j);
                    try {
                        com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaContract.class
                                .getDeclaredMethod(method.getName(), method.getParameterTypes());
                        continue;
                    } catch (NoSuchMethodException expected) { ++removed; }
                }
                if (index == 2) scenario.check("retiredMethods", 2, removed);
                checkRetiredLambdaMethods(scenario, instance, retired, handles, "");
                System.gc(); System.runFinalization(); System.gc();
                scenario.check("afterGc", values[index], instance.value());
                checkRetiredLambdaMethods(scenario, instance, retired, handles, "AfterGc");
                if (index == 2) scenario.expectReflectionError("bytecodeBridgeAfterGc",
                        () -> bytecodeCaller[0].invoke(null, instance));
            });
        }
    }

    private static void checkRetiredLambdaMethods(Scenario scenario, Object instance,
            List<Method> methods, List<MethodHandle> handles, String suffix) throws Exception {
        int privateCount = 0, bridgeCount = 0;
        for (int i = 0; i < methods.size(); ++i) {
            Method method = methods.get(i);
            try {
                com.github.ytlog.artdcevm.fixture.interfacelambda.LambdaContract.class
                        .getDeclaredMethod(method.getName(), method.getParameterTypes());
                continue;
            } catch (NoSuchMethodException expected) { /* The cached target was retired. */ }
            String kind = Modifier.isPrivate(method.getModifiers()) ? "Private" : "Synthetic";
            if (kind.equals("Private")) ++privateCount; else ++bridgeCount;
            final int target = i;
            scenario.expectReflectionError("retiredReflection" + kind + suffix, () -> method.invoke(null, instance));
            scenario.expectError("retiredHandle" + kind + suffix, NoSuchMethodError.class,
                    () -> handles.get(target).invokeWithArguments(instance));
            scenario.expectError("retiredJni" + kind + suffix, NoSuchMethodError.class,
                    () -> StandardJvmtiClient.callInterfaceStatic(instance, target));
        }
        if (!methods.isEmpty()) {
            scenario.check("retiredPrivate" + suffix, 1, privateCount);
            scenario.check("retiredSynthetic" + suffix, 1, bridgeCount);
        }
    }

    private void runInterfaceStressAdditionScenario() throws Exception {
        InterfaceStressTypes.Child[] instances = new InterfaceStressTypes.Child[200];
        Class<?>[] types = new Class<?>[200];
        for (int i = 0; i < instances.length; ++i) {
            types[i] = Class.forName(InterfaceStressTypes.class.getName() + "$C"
                    + String.format(java.util.Locale.ROOT, "%03d", i));
            instances[i] = (InterfaceStressTypes.Child) types[i].getConstructor().newInstance();
            instances[i].marker = i;
        }
        runScenario("interface-stress-add", 0, scenario -> {
            scenario.redefine(new Class<?>[]{StressContract.class}, "interface-stress-add");
            Method added = StressContract.class.getMethod("added");
            scenario.check("snapshotTypes", 207L, DcevmRuntime.capabilities().getLong("interfaceSnapshotTypes"));
            for (int i = 0; i < instances.length; ++i) {
                scenario.check("oldAdded" + i, 18, added.invoke(instances[i]));
                scenario.check("oldDispatch" + i, 8, instances[i].value());
                scenario.check("oldMarker" + i, i, instances[i].marker);
                scenario.check("newAdded" + i, 18, added.invoke(types[i].getConstructor().newInstance()));
            }
            System.gc(); System.runFinalization(); System.gc();
            for (int i = 0; i < instances.length; ++i)
                scenario.check("afterGc" + i, 18, added.invoke(instances[i]));
        });
    }

    // Isolated Debug diagnostic: preload the same 200 implementations for both
    // ordinary multi-target snapshots and the interface-stress transaction.
    private void runSnapshotBenchmark() throws Exception {
        for (int i = 0; i < 200; ++i)
            Class.forName(InterfaceStressTypes.class.getName() + "$C"
                    + String.format(java.util.Locale.ROOT, "%03d", i));
        for (int i = 0; i < 10; ++i)
            runScenario("snapshot-ordinary-" + i, 0, scenario -> {
                scenario.redefine(new Class<?>[]{Evolving.class, ModifierSubject.class},
                        "body", "modifier-public");
                scenario.check("body", 12, new Evolving(10).compute());
                scenario.check("member", 43, new ModifierSubject().throughPublic());
            });
        runInterfaceStressScenario();
    }

    private void runInterfaceStressScenario() throws Exception {
        InterfaceStressTypes.Child[] instances = new InterfaceStressTypes.Child[200];
        Class<?>[] types = new Class<?>[instances.length];
        int baseline = 0;
        for (int i = 0; i < instances.length; ++i) {
            String name = InterfaceStressTypes.class.getName() + "$C" + String.format(java.util.Locale.ROOT, "%03d", i);
            types[i] = Class.forName(name);
            instances[i] = (InterfaceStressTypes.Child) types[i].getConstructor().newInstance();
            instances[i].marker = i;
            baseline += instances[i].value();
        }
        final int before = baseline;
        runScenario("interface-stress", 0, scenario -> {
            scenario.check("baseline", 1400, before);
            long retries = DcevmRuntime.capabilities().getLong("subclassLoadRetries");
            scenario.redefine(new Class<?>[]{StressContract.class}, "interface-stress");
            JSONObject capabilities = DcevmRuntime.capabilities();
            long snapshot = capabilities.getLong("interfaceSnapshotNanos");
            long allocation = capabilities.getLong("interfaceAllocationNanos");
            scenario.result.put("interfaceTiming", new JSONObject()
                    .put("types", capabilities.getLong("interfaceSnapshotTypes"))
                    .put("snapshotNanos", snapshot).put("allocationNanos", allocation));
            // Root + four subinterfaces + two superclasses + 200 implementations.
            scenario.check("snapshotTypes", 207L, capabilities.getLong("interfaceSnapshotTypes"));
            scenario.check("runtimeProbed", true,
                    capabilities.getString("interfaceRuntime").startsWith("verified: "));
            scenario.check("snapshotMeasured", true, snapshot > 0);
            scenario.check("allocationMeasured", true, allocation > 0);
            scenario.check("noUnexpectedRetry", retries, capabilities.getLong("subclassLoadRetries"));
            for (int i = 0; i < instances.length; ++i) {
                scenario.check("oldDispatch" + i, 8, instances[i].value());
                scenario.check("oldMarker" + i, i, instances[i].marker);
                InterfaceStressTypes.Child fresh = (InterfaceStressTypes.Child) types[i].getConstructor().newInstance();
                scenario.check("newDispatch" + i, 8, fresh.value());
                scenario.check("newMarker" + i, 17, fresh.marker);
            }
            System.gc(); System.runFinalization(); System.gc();
            for (int i = 0; i < instances.length; ++i)
                scenario.check("dispatchAfterGc" + i, 8, instances[i].value());
        });
    }

    // Interfaces added to a class with instances and a loaded subclass. AddedContract
    // and NestedContract are not loaded before the redefinition: the engine
    // must resolve them through the class loader before ART links the class.
    private void runInterfaceScenarios() throws Exception {
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceChild child = new InterfaceChild();
        String schema = memberSchema(InterfaceSubject.class);
        String[][] invalid = {{"interface-hidden", "inaccessible"}, {"interface-class", "class"},
                {"interface-missing", "missing"}};
        for (String[] variant : invalid) {
            runScenario(variant[0], 62, scenario -> {
                scenario.redefine(new Class<?>[]{InterfaceSubject.class}, variant[0]);
                scenario.check("interfaceCount", 0, InterfaceSubject.class.getInterfaces().length);
                scenario.check("unchangedMembers", schema, memberSchema(InterfaceSubject.class));
                scenario.check("contract", 51, subject.contract());
            });
        }
        String contract = "Lcom/github/ytlog/artdcevm/fixture/AddedContract;";
        String nested = "Lcom/github/ytlog/artdcevm/fixture/other/ContractHost$NestedContract;";
        String interfaces = "com.github.ytlog.artdcevm.fixture.AddedContract,"
                + IntSupplier.class.getName()
                + ",com.github.ytlog.artdcevm.fixture.other.ContractHost$NestedContract";
        runScenario("interface-add", 0, scenario -> {
            scenario.check("contractLoadedBefore", false, StandardJvmtiClient.isLoaded(contract));
            scenario.check("nestedLoadedBefore", false, StandardJvmtiClient.isLoaded(nested));
            scenario.redefine(new Class<?>[]{InterfaceSubject.class}, "interface-add");
            checkAddedInterfaces(scenario, subject, child, interfaces);
            scenario.check("unchangedMembers", schema, memberSchema(InterfaceSubject.class));
            scenario.check("contractLoadedAfter", true, StandardJvmtiClient.isLoaded(contract));
            scenario.check("newContractTwice", 102, InterfaceCaller.contractTwice(new InterfaceSubject()));
        });
        // Removing them again narrows the existing instances; the checkcast in
        // InterfaceCaller now fails for both.
        runScenario("interface-remove", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceSubject.class}, "interface-original");
            scenario.check("interfaceCount", 0, InterfaceSubject.class.getInterfaces().length);
            scenario.check("oldIsContract", false, InterfaceCaller.isContract(subject));
            scenario.check("childIsContract", false, InterfaceCaller.isContract(child));
            scenario.expectError("oldContractTwice", ClassCastException.class,
                    () -> InterfaceCaller.contractTwice(subject));
            scenario.check("oldIsIntSupplier", false, IntSupplier.class.isInstance(subject));
            scenario.check("contract", 51, subject.contract());
            scenario.check("unchangedMembers", schema, memberSchema(InterfaceSubject.class));
        });
    }

    private static void checkAddedInterfaces(Scenario scenario, InterfaceSubject subject,
            InterfaceChild child, String interfaces) throws Exception {
        StringBuilder names = new StringBuilder();
        for (Class<?> type : InterfaceSubject.class.getInterfaces()) {
            if (names.length() > 0) names.append(',');
            names.append(type.getName());
        }
        scenario.check("interfaces", interfaces, names.toString());
        scenario.check("oldIsContract", true, InterfaceCaller.isContract(subject));
        scenario.check("childIsContract", true, InterfaceCaller.isContract(child));
        scenario.check("oldContractTwice", 102, InterfaceCaller.contractTwice(subject));
        scenario.check("childContractTwice", 122, InterfaceCaller.contractTwice(child));
        Object supplier = subject;
        scenario.check("oldIntSupplier", 52, ((IntSupplier) supplier).getAsInt());
        Method nestedMethod = InterfaceSubject.class.getInterfaces()[2].getMethod("nested");
        nestedMethod.setAccessible(true);
        scenario.check("oldNestedDefault", 7, nestedMethod.invoke(subject));
        scenario.check("childNestedDefault", 7, nestedMethod.invoke(child));
    }

    // SuperSubject (with a loaded subclass and instances of both) moves from
    // SuperBase to SuperMiddle, which extends SuperBase. Invalid superclasses
    // and narrowing back to SuperBase are rejected with the definition intact.
    private void runSuperclassScenarios() throws Exception {
        SuperSubject subject = new SuperSubject();
        SuperChild child = new SuperChild();
        String schema = memberSchema(SuperSubject.class);
        Object[][] invalid = {{"super-final", 62}, {"super-interface", 62}, {"super-missing", 62},
                {"super-final-override", 62}, {"super-circular", 61}};
        for (Object[] variant : invalid) {
            runScenario((String) variant[0], (Integer) variant[1], scenario -> {
                scenario.redefine(new Class<?>[]{SuperSubject.class}, (String) variant[0]);
                scenario.check("superclass", "com.github.ytlog.artdcevm.fixture.SuperBase",
                        SuperSubject.class.getSuperclass().getName());
                scenario.check("unchangedMembers", schema, memberSchema(SuperSubject.class));
                scenario.check("value", 12, subject.value());
            });
        }
        runScenario("super-widen", 0, scenario -> {
            scenario.check("describeBefore", "base", subject.describe());
            scenario.redefine(new Class<?>[]{SuperSubject.class}, "super-widen");
            checkWidenedSuperclass(scenario, subject, child);
            scenario.check("unchangedMembers", schema, memberSchema(SuperSubject.class));
            SuperSubject created = new SuperSubject();
            scenario.check("newMiddleValue", 11, ((SuperMiddle) (Object) created).middleValue);
            scenario.check("newValue", 12, created.value());
        });
        runScenario("super-narrow", 66, scenario -> {
            scenario.redefine(new Class<?>[]{SuperSubject.class}, "super-original");
            checkWidenedSuperclass(scenario, subject, child);
        });
        runSuperBatchScenarios();
        runSuperChainScenarios();
        runDescriptiveFlagScenarios();
        runKindChangeScenarios();
    }

    // Static/instance changes keep old bytecode calls protected until Caller is updated.
    private void runKindChangeScenarios() throws Exception {
        KindFixture.Subject subject = new KindFixture.Subject();
        subject.marker = 9;
        scenarioCheckCaller(KindFixture.Caller.call());
        Method staticConvert = KindFixture.Subject.class.getMethod("convert", int.class);
        runScenario("kind-instance", 0, scenario -> {
            scenario.redefine(new Class<?>[]{KindFixture.Subject.class}, "kind-instance");
            scenario.check("instanceKind", false, Modifier.isStatic(
                    KindFixture.Subject.class.getMethod("convert", int.class).getModifiers()));
            scenario.check("instanceCall", 4, KindFixture.Subject.class.getMethod("convert", int.class).invoke(subject, 2));
            scenario.expectError("oldStatic", IncompatibleClassChangeError.class, KindFixture.Caller::call);
            scenario.expectError("retiredStatic", NoSuchMethodError.class, () -> staticConvert.invoke(null, 1));
        });
        runScenario("kind-instance-batch", 0, scenario -> {
            scenario.redefine(new Class<?>[]{KindFixture.Subject.class, KindFixture.Caller.class},
                    "kind-instance", "kind-instance");
            Method convert = KindFixture.Subject.class.getMethod("convert", int.class);
            scenario.check("instanceKind", false, Modifier.isStatic(convert.getModifiers()));
            scenario.check("caller", 3, KindFixture.Caller.call());
            scenario.check("instanceCall", 4, convert.invoke(subject, 2));
            scenario.expectError("retiredStatic", NoSuchMethodError.class, () -> staticConvert.invoke(null, 1));
            scenario.check("marker", 9, subject.marker);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("callerAfterGc", 3, KindFixture.Caller.call());
        });
        runScenario("kind-private", 0, scenario -> {
            scenario.redefine(new Class<?>[]{KindFixture.Subject.class}, "kind-private");
            scenario.check("privateKind", true, Modifier.isPrivate(
                    KindFixture.Subject.class.getDeclaredMethod("helper").getModifiers()));
            scenario.check("useHelper", 6, subject.useHelper());
            scenario.check("marker", 9, subject.marker);
        });
        runScenario("kind-static", 0, scenario -> {
            scenario.redefine(new Class<?>[]{KindFixture.Subject.class}, "kind-static");
            scenario.check("staticKind", true, Modifier.isStatic(
                    KindFixture.Subject.class.getDeclaredMethod("convert", int.class).getModifiers()));
            scenario.check("staticCall", 4, KindFixture.Subject.class.getDeclaredMethod("convert", int.class).invoke(null, 1));
            scenario.expectError("oldCaller", IncompatibleClassChangeError.class, KindFixture.Caller::call);
        });
    }

    private static void scenarioCheckCaller(int value) {
        if (value != 2) throw new AssertionError("KindFixture.Caller precondition: " + value);
    }

    // Bridge, varargs, strict and synthetic flags only describe a method.
    private void runDescriptiveFlagScenarios() throws Exception {
        ModifierFlagsFixture.Subject subject = new ModifierFlagsFixture.Subject();
        runScenario("method-descriptive-flags", 0, scenario -> {
            scenario.check("varargsBefore", true,
                    ModifierFlagsFixture.Subject.class.getMethod("sum", int[].class).isVarArgs());
            scenario.redefine(new Class<?>[]{ModifierFlagsFixture.Subject.class}, "method-descriptive-flags");
            Method sum = ModifierFlagsFixture.Subject.class.getMethod("sum", int[].class);
            Method half = ModifierFlagsFixture.Subject.class.getMethod("half", double.class);
            scenario.check("varargsAfter", false, sum.isVarArgs());
            scenario.check("strictAfter", false, Modifier.isStrict(half.getModifiers()));
            scenario.check("sum", 7, subject.sum(2, 4));
            scenario.check("half", 2.5, subject.half(10));
        });
    }

    // A superclass change may share the batch with a subclass of the moving
    // class; that subclass verifies against the moving class's new version.
    private void runSuperBatchScenarios() throws Exception {
        SuperBatchFixture.Child child = new SuperBatchFixture.Child();
        child.childValue = 13; child.own = 17; child.baseValue = 19;
        String schema = memberSchema(SuperBatchFixture.Subject.class);
        runScenario("super-batch-moving", 66, scenario -> {
            // A subclass that changes its superclass too stays rejected.
            scenario.expectRejectionBy(SuperBatchFixture.Subject.class);
            scenario.redefine(new Class<?>[]{SuperBatchFixture.Subject.class, SuperBatchFixture.Child.class},
                    "super-batch-moving", "super-batch-moving");
            scenario.check("rejectionReason", true,
                    DcevmRuntime.capabilities().getString("lastRejection").contains("changes its superclass"));
            scenario.check("unchangedSubject", schema, memberSchema(SuperBatchFixture.Subject.class));
            scenario.check("superclassUnchanged", SuperBatchFixture.Base.class.getName(),
                    SuperBatchFixture.Subject.class.getSuperclass().getName());
            scenario.check("base", 19, child.base());
            scenario.check("childValue", 13, child.childValue);
        });
        runScenario("super-batch-child", 0, scenario -> {
            scenario.redefine(new Class<?>[]{SuperBatchFixture.Child.class, SuperBatchFixture.Subject.class},
                    "super-batch-child", "super-batch-child");
            scenario.check("subjectSuperclass", SuperBatchFixture.Middle.class.getName(),
                    SuperBatchFixture.Subject.class.getSuperclass().getName());
            scenario.check("childIsMiddle", true, ((Object) child) instanceof SuperBatchFixture.Middle);
            Method via = SuperBatchFixture.Child.class.getMethod("viaMiddle");
            scenario.check("existingViaMiddle", 53, via.invoke(child));
            scenario.check("newViaMiddle", 60, via.invoke(new SuperBatchFixture.Child()));
            scenario.check("existingMiddleValue", 0, SuperBatchFixture.Middle.class.getField("middleValue").getInt(child));
            scenario.check("own", 17, child.own);
            scenario.check("baseValue", 19, child.baseValue);
            scenario.check("childValue", 13, child.childValue);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("viaMiddleAfterGc", 53, via.invoke(child));
        });
    }

    // A superclass change may share the batch with its new superclass chain:
    // classes there that change only method bodies keep their identity, and a
    // structural one is rebuilt first so the target links under its
    // replacement. A structural supertype of the target stays rejected.
    private void runSuperChainScenarios() throws Exception {
        SuperChainFixture.Subject subject = new SuperChainFixture.Subject();
        subject.own = 17; subject.baseValue = 19;
        SuperChainFixture.Mover mover = new SuperChainFixture.Mover();
        mover.moverValue = 31;
        SuperChainFixture.DeepMover deepMover = new SuperChainFixture.DeepMover();
        deepMover.value = 37;
        String schema = memberSchema(SuperChainFixture.Subject.class);
        runScenario("super-chain-old-structural", 66, scenario -> {
            scenario.expectRejectionBy(SuperChainFixture.Base.class);
            scenario.redefine(new Class<?>[]{SuperChainFixture.Subject.class, SuperChainFixture.Base.class},
                    "super-chain-structural", "super-chain-structural");
            scenario.check("rejectionReason", true,
                    DcevmRuntime.capabilities().getString("lastRejection").contains("changes structurally"));
            scenario.check("unchangedSubject", schema, memberSchema(SuperChainFixture.Subject.class));
            scenario.check("superclassUnchanged", SuperChainFixture.Base.class.getName(),
                    SuperChainFixture.Subject.class.getSuperclass().getName());
            scenario.check("base", 19, subject.base());
        });
        runScenario("super-chain-body", 0, scenario -> {
            Class<?> middle = SuperChainFixture.Middle.class;
            SuperChainFixture.Middle published = new SuperChainFixture.Middle();
            scenario.redefine(new Class<?>[]{SuperChainFixture.Subject.class, middle, SuperChainFixture.Base.class},
                    "super-chain-body", "super-chain-body", "super-chain-body");
            scenario.check("middleIdentity", true, SuperChainFixture.Subject.class.getSuperclass() == middle);
            scenario.check("existingIsMiddle", true, ((Object) subject) instanceof SuperChainFixture.Middle);
            Method chain = SuperChainFixture.Subject.class.getMethod("chain");
            // New middle() and base() bodies; the existing instance's new Middle field is zero.
            scenario.check("existingChain", 87, chain.invoke(subject));
            scenario.check("newChain", 74, chain.invoke(new SuperChainFixture.Subject()));
            scenario.check("publishedMiddle", 61, published.middle());
            scenario.check("publishedBase", 6, published.base());
            scenario.check("own", 17, subject.own);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("existingChainAfterGc", 87, chain.invoke(subject));
        });
        runScenario("super-chain-rebuilt", 0, scenario -> {
            // Middle gains members; Mover moves under it and DeepMover under Deep,
            // which ART rebuilds as Middle's subclass.
            Class<?> middle = SuperChainFixture.Middle.class;
            scenario.redefine(new Class<?>[]{SuperChainFixture.Mover.class, SuperChainFixture.DeepMover.class, middle},
                    "super-chain-rebuilt", "super-chain-rebuilt", "super-chain-rebuilt");
            scenario.check("moverIdentity", true, (Object) SuperChainFixture.Mover.class.getSuperclass() == middle);
            scenario.check("deepMoverIdentity", true,
                    (Object) SuperChainFixture.DeepMover.class.getSuperclass() == SuperChainFixture.Deep.class);
            scenario.check("middleExtra", "int", middle.getField("extra").getType().getName());
            Method viaMiddle = SuperChainFixture.Mover.class.getMethod("viaMiddle");
            Method viaDeep = SuperChainFixture.DeepMover.class.getMethod("viaDeep");
            // The existing instances' new superclass fields are zero.
            scenario.check("existingMover", 181, viaMiddle.invoke(mover));
            scenario.check("newMover", 184, viaMiddle.invoke(new SuperChainFixture.Mover()));
            scenario.check("existingDeepMover", 137, viaDeep.invoke(deepMover));
            scenario.check("newDeepMover", 142, viaDeep.invoke(new SuperChainFixture.DeepMover()));
            scenario.check("subjectChain", 87, SuperChainFixture.Subject.class.getMethod("chain").invoke(subject));
            scenario.check("moverValue", 31, mover.moverValue);
            System.gc(); System.runFinalization(); System.gc();
            scenario.check("existingMoverAfterGc", 181, viaMiddle.invoke(mover));
            scenario.check("existingDeepMoverAfterGc", 137, viaDeep.invoke(deepMover));
        });
    }

    // ART recreates the target's loaded subclasses before verifying the new
    // DEX, and lets this thread load classes while it verifies. A subclass that
    // verification first loads must still be recreated under the new layout:
    // preloaded when the submitted DEX names it, otherwise (here named only by
    // a ClassFileLoadHook replacement) detected and retried.
    private void runPreloadScenarios() throws Exception {
        String child = "Lcom/github/ytlog/artdcevm/fixture/PreloadChild;";
        runScenario("preload-subclass", 0, scenario -> {
            scenario.check("childLoadedBefore", false, StandardJvmtiClient.isLoaded(child));
            scenario.redefine(new Class<?>[]{PreloadSubject.class}, "preload-subclass");
            scenario.check("childLoadedAfter", true, StandardJvmtiClient.isLoaded(child));
            scenario.check("childLayout", 435, PreloadSubject.probe());
        });
        String hookChild = "Lcom/github/ytlog/artdcevm/fixture/PreloadHookChild;";
        runScenario("preload-hook-subclass", 0, scenario -> {
            scenario.check("childLoadedBefore", false, StandardJvmtiClient.isLoaded(hookChild));
            long retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            long replacements = StandardJvmtiClient.classFileReplacements();
            scenario.check("hookInstalled", 0, StandardJvmtiClient.setClassFileReplacement(
                    "com/github/ytlog/artdcevm/fixture/PreloadHookSubject",
                    readAsset("preload-hook/com/github/ytlog/artdcevm/fixture/PreloadHookSubject.dex")));
            try {
                scenario.redefine(new Class<?>[]{PreloadHookSubject.class}, "preload-hook-original");
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            scenario.check("hookReplacements", 2L, StandardJvmtiClient.classFileReplacements() - replacements);
            scenario.check("subclassLoadRetries", 1L,
                    DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries);
            scenario.check("childLoadedAfter", true, StandardJvmtiClient.isLoaded(hookChild));
            scenario.check("childLayout", 435, PreloadHookSubject.probe());
        });
    }

    // A hot update through the tool protocol (HotReloadTool): the tool appends
    // the classes the APK lacks (a class, a subclass of a redefined class and a
    // lambda), loads an APK class nothing has used and redefines the existing
    // ones; a later update redefines a class the tool added.
    private void runToolScenarios() throws Exception {
        String fixture = "com/github/ytlog/artdcevm/fixture";
        String added = "L" + fixture + "/ToolAdded;";
        String dormant = "L" + fixture + "/ToolDormant;";
        ToolHost old = new ToolHost();
        runScenario("tool-add-classes", 0, scenario -> {
            scenario.check("addedLoadedBefore", false, StandardJvmtiClient.isLoaded(added));
            scenario.check("dormantLoadedBefore", false, StandardJvmtiClient.isLoaded(dormant));
            HotReloadTool tool = new HotReloadTool(context).prepare("tool-v1", fixture);
            scenario.check("classified", "added=ToolAdded,ToolChild,ToolHost$$ExternalSyntheticLambda0;"
                    + "changed=ToolDormant,ToolHost", tool.classification());
            scenario.check("appendCode", 0, tool.appendCode);
            long retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            scenario.redefine(tool.existingClasses(), "tool-v1", "tool-v1");
            // The engine preloads ToolChild, which the submitted DEX names.
            scenario.check("subclassLoadRetries", 0L,
                    DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries);
            scenario.check("run", 1443, ToolHost.run());
            scenario.check("oldOwn", 6, old.own);
            scenario.check("oldDescribe", 1, ToolHost.class.getMethod("describe").invoke(old));
            Class<?> updated = Class.forName(fixture.replace('/', '.') + ".ToolDormant", false,
                    context.getClassLoader());
            scenario.check("dormantValue", 2, updated.getMethod("value").invoke(null));
        });
        runScenario("tool-redefine-added", 0, scenario -> {
            HotReloadTool tool = new HotReloadTool(context).prepare("tool-v2", fixture);
            scenario.check("classified", "added=;changed=ToolAdded", tool.classification());
            scenario.check("appendCode", -1, tool.appendCode);
            scenario.redefine(tool.existingClasses(), "tool-v2");
            scenario.check("run", 1543, ToolHost.run());
        });
    }

    private static void checkWidenedSuperclass(Scenario scenario, SuperSubject subject,
            SuperChild child) throws Exception {
        scenario.check("superclass", SuperMiddle.class.getName(),
                SuperSubject.class.getSuperclass().getName());
        Object old = subject;
        Object oldChild = child;
        scenario.check("oldIsMiddle", true, old instanceof SuperMiddle);
        scenario.check("childIsMiddle", true, oldChild instanceof SuperMiddle);
        scenario.check("oldOwn", 7, subject.own);
        scenario.check("oldBase", 5, subject.baseValue);
        scenario.check("oldValue", 12, subject.value());
        scenario.check("oldMiddleValue", 0, ((SuperMiddle) old).middleValue);
        scenario.check("oldMiddle", 40, ((SuperMiddle) old).middle());
        scenario.check("oldDescribe", "middle", subject.describe());
        scenario.check("childValue", 9, child.childValue);
        scenario.check("childOwn", 7, child.own);
        scenario.check("childBase", 5, child.baseValue);
        scenario.check("childDescribe", "middle", child.describe());
    }

    // Native, constructor, abstract and initializer declarations retire like
    // other methods; native -> Java and abstract <-> concrete change kinds.
    private void runMethodKindScenarios() throws Exception {
        NativeSubject natives = new NativeSubject();
        Method cachedNative = NativeSubject.class.getDeclaredMethod("nativeValue");
        Method cachedToJava = NativeSubject.class.getDeclaredMethod("nativeToJava");
        runScenario("native-change", 0, scenario -> {
            scenario.redefine(new Class<?>[]{NativeSubject.class}, "native-change");
            scenario.check("other", 6, natives.other());
            scenario.check("toJavaCall", 95, NativeCaller.nativeToJava(natives));
            scenario.check("toJavaCachedReflection", 95, cachedToJava.invoke(natives));
            scenario.check("toJavaIsNative", false, Modifier.isNative(
                    NativeSubject.class.getDeclaredMethod("nativeToJava").getModifiers()));
            scenario.expectError("removedNativeLookup", NoSuchMethodException.class,
                    () -> NativeSubject.class.getDeclaredMethod("nativeValue"));
            scenario.expectReflectionError("removedNativeCachedReflection",
                    () -> cachedNative.invoke(natives));
            scenario.expectError("removedNativeCall", NoSuchMethodError.class,
                    () -> NativeCaller.nativeValue(natives));
        });
        ConstructorSubject constructed = new ConstructorSubject(9);
        java.lang.reflect.Constructor<ConstructorSubject> cachedConstructor =
                ConstructorSubject.class.getDeclaredConstructor(int.class);
        StandardJvmtiClient.cacheConstructor(ConstructorSubject.class, "(I)V");
        runScenario("constructor-delete", 0, scenario -> {
            scenario.check("jniBefore", 4,
                    ((ConstructorSubject) StandardJvmtiClient.newObjectCached(4)).value);
            scenario.redefine(new Class<?>[]{ConstructorSubject.class}, "constructor-delete");
            scenario.check("oldValue", 9, constructed.value);
            scenario.check("newDefault", 2, new ConstructorSubject().value);
            scenario.expectError("removedLookup", NoSuchMethodException.class,
                    () -> ConstructorSubject.class.getDeclaredConstructor(int.class));
            scenario.expectReflectionError("cachedConstructor", () -> cachedConstructor.newInstance(7));
            scenario.expectError("cachedJni", NoSuchMethodError.class,
                    () -> StandardJvmtiClient.newObjectCached(7));
            scenario.expectError("bytecodeNew", NoSuchMethodError.class,
                    () -> ConstructorCaller.create(5));
        });
        AbstractMethodChild abstractChild = new AbstractMethodChild();
        Method cachedAbstract = AbstractMethodSubject.class.getDeclaredMethod("abstractValue");
        runAbstractScenarios(abstractChild, cachedAbstract);
        int initialized = ClinitSubject.value;
        runScenario("clinit-delete", 0, scenario -> {
            scenario.check("valueBefore", 41, initialized);
            scenario.redefine(new Class<?>[]{ClinitSubject.class}, "clinit-delete");
            scenario.check("valueAfter", 41, ClinitSubject.value);
            scenario.check("computeAfter", 41, ClinitSubject.compute());
        });
    }

    private void runAbstractScenarios(AbstractMethodChild child, Method cachedAbstract) throws Exception {
        runScenario("abstract-method-delete", 0, scenario -> {
            scenario.check("callBefore", 19, AbstractCaller.abstractValue(child));
            scenario.redefine(new Class<?>[]{AbstractMethodSubject.class}, "methods-abstract-delete");
            scenario.check("ownValue", 19, child.abstractValue());
            scenario.check("concreteValue", 18, child.concreteValue());
            scenario.check("staticMarker", 17, AbstractMethodSubject.marker);
            scenario.expectError("removedLookup", NoSuchMethodException.class,
                    () -> AbstractMethodSubject.class.getDeclaredMethod("abstractValue"));
            scenario.expectReflectionError("cachedReflection", () -> cachedAbstract.invoke(child));
            scenario.expectError("callThroughRemoved", NoSuchMethodError.class,
                    () -> AbstractCaller.abstractValue(child));
        });
        runScenario("abstract-method-make-abstract", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractMethodSubject.class}, "methods-abstract-concrete");
            scenario.check("concreteIsAbstract", true, Modifier.isAbstract(
                    AbstractMethodSubject.class.getDeclaredMethod("concreteValue").getModifiers()));
            scenario.expectError("unimplemented", AbstractMethodError.class, child::concreteValue);
            scenario.check("ownValue", 19, child.abstractValue());
        });
        runScenario("abstract-method-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AbstractMethodSubject.class}, "methods-abstract-original");
            scenario.check("concreteValue", 17, child.concreteValue());
            scenario.check("callRestored", 19, AbstractCaller.abstractValue(child));
            scenario.check("newChild", 17, new AbstractMethodChild().concreteValue());
        });
    }

    private void runMethodScenarios(ClassFinalSubject classFinal, int classFlags,
            String classSchema) throws Exception {
        MethodSubject subject = new MethodSubject();
        subject.value = 60;
        subject.keep = 70;
        MethodSubject.staticKeep = 80;
        Object reference = subject.object;
        Object staticReference = MethodSubject.staticObject;
        MethodTargets targets = new MethodTargets();
        String schema = memberSchema(MethodSubject.class);
        runScenario("atomic-method-retire-class-interface", 70, scenario -> {
            checkMethodBaseline(scenario, subject, targets);
            scenario.expectRejectionBy(ClassFinalSubject.class);
            scenario.redefine(new Class<?>[]{MethodSubject.class, ClassFinalSubject.class},
                    "methods-delete", "class-interface");
            scenario.check("unchangedMethodMembers", schema, memberSchema(MethodSubject.class));
            scenario.check("unchangedStaticCall", 82, MethodCaller.callStatic());
            scenario.check("unchangedVirtualCall", 63, MethodCaller.callVirtual(subject));
            scenario.check("unchangedSignatureCall", 64, MethodCaller.callSignature(subject));
            checkClassFinalUnchanged(scenario, classFinal, classFlags, classSchema);
        });
        runMethodKindScenarios();
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

    // The runtime reports whether named switch-interpreter selectors are installed;
    // a runtime that inlines them keeps rejection 71 for active self frames.
    private static boolean privateSelfSelection() throws Exception {
        return DcevmRuntime.capabilities().optBoolean("privateSelfSelection", false);
    }

    private void runPrivateSelfScenarios() throws Exception {
        boolean supported = privateSelfSelection();
        for (int round = 0; round < 2; round++) {
            for (boolean iface : new boolean[]{false, true}) {
                PrivateSelfFixture.Subject subject = new PrivateSelfFixture.Subject();
                PrivateSelfFixture.Child child = new PrivateSelfFixture.Child();
                PrivateSelfFixture.Impl impl = new PrivateSelfFixture.Impl();
                PrivateSelfFixture.Override override = new PrivateSelfFixture.Override();
                Class<?> target = iface ? PrivateSelfFixture.Contract.class : PrivateSelfFixture.Subject.class;
                String name = "method-private-self-" + (iface ? "interface" : "class") + (round == 0 ? "" : "-again");
                CountDownLatch open = new CountDownLatch(0);
                for (int i = 0; i < 30000; i++) {
                    if ((iface ? impl.await(open, open, 0) : subject.await(open, open, 0)) != (iface ? 11 : 7) ||
                            (iface ? override.await(open, open, 0) : child.await(open, open, 0)) != (iface ? 49 : 47) ||
                            (iface ? impl.await(open, open, 1) : subject.await(open, open, 1)) != (iface ? 6 : 11))
                        throw new AssertionError("self warmup");
                }
                CountDownLatch ready = new CountDownLatch(4), resume = new CountDownLatch(1);
                Thread[] workers = new Thread[4];
                Object[] results = new Object[4];
                boolean[] stepping = new boolean[4];
                boolean singleStep = round == 1;
                for (int i = 0; i < workers.length; i++) {
                    final int slot = i;
                    workers[i] = new Thread(() -> {
                        try {
                            stepping[slot] = !singleStep || StandardJvmtiClient.setThreadSingleStep(true) == 0;
                            int mode = slot == 2 ? 1 : slot == 3 ? 2 : 0;
                            results[slot] = iface ? (slot == 1 ? override : impl).await(ready, resume, mode)
                                    : (slot == 1 ? child : subject).await(ready, resume, mode);
                        } catch (Throwable error) { results[slot] = error.getClass().getName(); }
                        finally { if (singleStep) stepping[slot] &= StandardJvmtiClient.setThreadSingleStep(false) == 0; }
                    });
                    workers[i].start();
                }
                try {
                    if (!ready.await(10, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("self frames not ready");
                    runScenario(name, supported ? 0 : 71, scenario -> {
                        scenario.redefine(new Class<?>[]{target}, "method-private-self");
                        if (!supported)
                            scenario.check("activeReason", true, DcevmRuntime.capabilities().getString("lastRejection").contains("active frame"));
                        scenario.check("privateKind", supported, Modifier.isPrivate(target.getDeclaredMethod("target").getModifiers()));
                        System.gc(); resume.countDown();
                        for (Thread worker : workers) worker.join(10000);
                        for (int i = 0; i < workers.length; i++) {
                            scenario.check("completed" + i, true, !workers[i].isAlive());
                            scenario.check("stepping" + i, true, stepping[i]);
                        }
                        scenario.check("self", supported ? (iface ? 31 : 25) : (iface ? 11 : 7), results[0]);
                        scenario.check("overrideReceiver", supported ? (iface ? 31 : 25) : (iface ? 49 : 47), results[1]);
                        scenario.check("range", supported ? (iface ? 36 : 41) : (iface ? 6 : 11), results[2]);
                        scenario.check("nullReceiver", NullPointerException.class.getName(), results[3]);
                        scenario.check("override", iface ? 49 : 47, iface ? override.target() : child.target());
                        if (!iface) {
                            scenario.check("entered", supported ? 3 : 0, PrivateSelfFixture.Subject.entered.get());
                            scenario.check("value", 5, subject.value);
                        }
                    });
                } finally {
                    resume.countDown(); for (Thread worker : workers) worker.join(10000);
                }
                runScenario(name + "-restore", 0, scenario -> {
                    scenario.redefine(new Class<?>[]{target}, "method-private-self-original");
                    PrivateSelfFixture.Subject.entered.set(0);
                    System.gc();
                    scenario.check("self", iface ? 11 : 7, iface ? impl.await(open, open, 0) : subject.await(open, open, 0));
                    scenario.check("overrideReceiver", iface ? 49 : 47, iface ? override.await(open, open, 0) : child.await(open, open, 0));
                });
            }
        }
    }

    private void runPrivateCallScenarios() throws Exception {
        AccessSubject subject = new AccessSubject();
        AccessCaller.Child child = new AccessCaller.Child();
        AccessCaller.Override override = new AccessCaller.Override();
        CountDownLatch selfReady = new CountDownLatch(1), selfResume = new CountDownLatch(1);
        Object[] selfResult = new Object[1];
        Thread own = new Thread(() -> {
            try { selfResult[0] = subject.await(selfReady, selfResume); }
            catch (Throwable t) { selfResult[0] = t.getClass().getName(); }
        });
        own.start();
        boolean supported = privateSelfSelection();
        try {
            if (!selfReady.await(10, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("self not ready");
            runScenario("method-private-active-target", supported ? 0 : 71, scenario -> {
                scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private");
                if (!supported)
                    scenario.check("activeReason", true, DcevmRuntime.capabilities().getString("lastRejection").contains("active frame"));
                scenario.check("privateKind", supported, Modifier.isPrivate(AccessSubject.class.getDeclaredMethod("target").getModifiers()));
                selfResume.countDown(); own.join(10000);
                scenario.check("activeCompleted", true, !own.isAlive());
                scenario.check("oldSelf", supported ? 25 : 7, selfResult[0]);
            });
        } finally { selfResume.countDown(); own.join(10000); }
        runScenario("method-private-active-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private-original");
            AccessSubject.entered = 0;
            scenario.check("restored", 7, subject.callTarget());
        });
        for (int cycle = 0; cycle < 2; cycle++) {
            String suffix = cycle == 0 ? "" : "-again";
            for (int i = 0; i < 30000; i++) {
                if (AccessCaller.callTarget(subject) != 7 || AccessCaller.inherited(child) != 7 ||
                        child.throughSuper() != 7 || AccessCaller.range(subject) != 11 || AccessCaller.overridden(override) != 47)
                    throw new AssertionError("private warmup");
            }
            runScenario("method-private" + suffix, 0, scenario -> {
                CountDownLatch ready = new CountDownLatch(1), resume = new CountDownLatch(1);
                Throwable[] failure = new Throwable[1];
                Thread active = new Thread(() -> {
                    try { AccessCaller.await(subject, ready, resume); } catch (Throwable t) { failure[0] = t; }
                });
                active.start();
                try {
                    if (!ready.await(10, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("external not ready");
                    scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private");
                    resume.countDown(); active.join(10000);
                    scenario.check("activeCompleted", true, !active.isAlive());
                    scenario.check("activeError", IllegalAccessError.class.getName(), failure[0] == null ? "" : failure[0].getClass().getName());
                    scenario.check("privateKind", true, Modifier.isPrivate(AccessSubject.class.getDeclaredMethod("target").getModifiers()));
                    scenario.check("hotFailures", 30000, AccessCaller.hotFailures(subject, 30000));
                    for (int i = 0; i < 32; i++) {
                        scenario.expectError("virtual" + i, IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
                        scenario.expectError("inherited" + i, IllegalAccessError.class, () -> AccessCaller.inherited(child));
                        scenario.expectError("super" + i, IllegalAccessError.class, child::throughSuper);
                        scenario.expectError("range" + i, IllegalAccessError.class, () -> AccessCaller.range(subject));
                        scenario.expectError("baseOverride" + i, IllegalAccessError.class, () -> AccessCaller.callTarget(override));
                    }
                    scenario.expectError("nullReceiver", IllegalAccessError.class, () -> AccessCaller.callTarget(null));
                    scenario.check("oldCallsDidNotEnter", 0, AccessSubject.entered);
                    scenario.check("ownCall", 25, subject.callTarget());
                    scenario.check("ownCallOverride", 25, override.callTarget());
                    scenario.check("override", 47, AccessCaller.overridden(override));
                    Method target = AccessSubject.class.getDeclaredMethod("target"); target.setAccessible(true);
                    Method range = AccessSubject.class.getDeclaredMethod("range", long.class, long.class, long.class); range.setAccessible(true);
                    scenario.check("newPrivate", 25, target.invoke(subject));
                    scenario.check("newRange", 41, range.invoke(subject, 1L, 2L, 3L));
                    long before = StandardJvmtiClient.singleStepCount();
                    scenario.check("singleStepEnable", 0, StandardJvmtiClient.setThreadSingleStep(true));
                    try {
                        scenario.expectError("interpreterVirtual", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
                        scenario.expectError("interpreterRange", IllegalAccessError.class, () -> AccessCaller.range(subject));
                    } finally { scenario.check("singleStepDisable", 0, StandardJvmtiClient.setThreadSingleStep(false)); }
                    scenario.check("singleStepObserved", true, StandardJvmtiClient.singleStepCount() > before);
                    System.gc();
                    scenario.expectError("afterGc", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
                    scenario.check("entered", 4, AccessSubject.entered);
                    scenario.check("value", 5, subject.value);
                } finally { resume.countDown(); active.join(10000); }
            });
            runScenario("method-private-caller-rebuild" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{AccessCallerBase.class}, "method-private-caller-rebuild");
                scenario.check("hotFailures", 30000, AccessCaller.hotFailures(subject, 30000));
                scenario.check("entered", 4, AccessSubject.entered);
            });
            runScenario("method-private-delete" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private-delete");
                scenario.expectError("oldVirtual", NoSuchMethodError.class, () -> AccessCaller.callTarget(subject));
                scenario.expectError("oldRange", NoSuchMethodError.class, () -> AccessCaller.range(subject));
                scenario.check("self", 55, subject.callTarget());
            });
            runScenario("method-private-readd" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private");
                AccessSubject.entered = 0;
                scenario.expectError("oldVirtual", IllegalAccessError.class, () -> AccessCaller.callTarget(subject));
                scenario.check("hotFailures", 30000, AccessCaller.hotFailures(subject, 30000));
                scenario.check("entered", 0, AccessSubject.entered);
            });
            runScenario("method-private-restore" + suffix, 0, scenario -> {
                scenario.redefine(new Class<?>[]{AccessSubject.class, AccessCallerBase.class},
                        "method-private-original", "method-private-original");
                scenario.check("virtual", 7, AccessCaller.callTarget(subject));
                scenario.check("range", 11, AccessCaller.range(subject));
                scenario.check("super", 7, child.throughSuper());
                AccessSubject.entered = 0;
            });
        }
        InterfaceInvokeKindFixture.Impl impl = new InterfaceInvokeKindFixture.Impl();
        AccessCaller.InterfaceOverride interfaceOverride = new AccessCaller.InterfaceOverride();
        for (int i = 0; i < 30000; i++) {
            if (AccessCaller.interfaceCall(impl) != 11 || AccessCaller.interfaceVirtual(impl) != 11 ||
                    AccessCaller.interfaceCall(interfaceOverride) != 49) throw new AssertionError("private interface warmup");
        }
        runScenario("method-private-interface", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceInvokeKindFixture.Contract.class}, "interface-kind-private");
            for (int i = 0; i < 32; i++) {
                scenario.expectError("interface" + i, IllegalAccessError.class, () -> AccessCaller.interfaceCall(impl));
                scenario.expectError("interfaceOverride" + i, IllegalAccessError.class, () -> AccessCaller.interfaceCall(interfaceOverride));
                scenario.expectError("virtual" + i, NoSuchMethodError.class, () -> AccessCaller.interfaceVirtual(impl));
            }
            Method ownCall = InterfaceInvokeKindFixture.Contract.class.getMethod("selfValue");
            scenario.check("ownCall", 31, ownCall.invoke(impl));
            scenario.check("ownCallOverride", 31, ownCall.invoke(interfaceOverride));
            scenario.check("override", 49, AccessCaller.interfaceOverride(interfaceOverride));
            System.gc();
            scenario.expectError("afterGc", IllegalAccessError.class, () -> AccessCaller.interfaceCall(impl));
        });
        runScenario("method-private-interface-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceInvokeKindFixture.Contract.class}, "interface-kind-original");
            scenario.check("interface", 11, AccessCaller.interfaceCall(impl));
            scenario.check("virtual", 11, AccessCaller.interfaceVirtual(impl));
        });
        runPrivateSelfScenarios();
    }

    // Invocation guards cover old bytecode, including retired identities re-added
    // as a different kind. Obsolete self selection requires named runtime services.
    private void runMethodKindChangeScenarios() throws Exception {
        runPrivateCallScenarios();
        StorageSubject subject = new StorageSubject();
        Method cachedStatic = StorageSubject.class.getDeclaredMethod("staticValue");
        runScenario("method-storage", 0, scenario -> {
            scenario.check("callerStaticBefore", 32, StorageCaller.callStatic());
            scenario.check("callerVirtualBefore", 8, StorageCaller.callVirtual(subject));
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage");
            scenario.check("staticValueIsStatic", false, Modifier.isStatic(
                    StorageSubject.class.getDeclaredMethod("staticValue").getModifiers()));
            scenario.expectError("callerStatic", IncompatibleClassChangeError.class, StorageCaller::callStatic);
            scenario.expectError("callerVirtual", IncompatibleClassChangeError.class,
                    () -> StorageCaller.callVirtual(subject));
            scenario.check("combined", 60, subject.combined());
        });
        runScenario("method-storage-delete", 0, scenario -> {
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage-delete");
            scenario.check("combined", 35, subject.combined());
            scenario.expectError("callerStatic", NoSuchMethodError.class, StorageCaller::callStatic);
            scenario.expectError("callerVirtual", NoSuchMethodError.class,
                    () -> StorageCaller.callVirtual(subject));
            scenario.expectReflectionError("cachedReflectionStatic", () -> cachedStatic.invoke(null));
        });
        // Deleted identities may return as the opposite kinds; old calls must recheck them.
        runScenario("method-storage-readd", 0, scenario -> {
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage");
            scenario.check("combined", 60, subject.combined());
            scenario.expectError("callerStatic", IncompatibleClassChangeError.class, StorageCaller::callStatic);
            scenario.expectError("callerVirtual", IncompatibleClassChangeError.class,
                    () -> StorageCaller.callVirtual(subject));
        });
        runScenario("method-storage-restore", 0, scenario -> {
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage-original");
            scenario.check("combined", 40, subject.combined());
            scenario.check("callerStatic", 32, StorageCaller.callStatic());
            scenario.check("callerVirtual", 8, StorageCaller.callVirtual(subject));
            scenario.expectReflectionError("cachedReflectionStatic", () -> cachedStatic.invoke(null));
        });
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
        JSONObject metadata = new JSONObject();
        for (Iterator<String> keys = report.keys(); keys.hasNext();) {
            String key = keys.next();
            if (!"scenarios".equals(key)) metadata.put(key, report.get(key));
        }
        String prefix = metadata.toString();
        try (FileOutputStream output = new FileOutputStream(temporary);
             BufferedOutputStream buffered = new BufferedOutputStream(output)) {
            buffered.write(prefix.substring(0, prefix.length() - 1).getBytes(StandardCharsets.UTF_8));
            buffered.write(",\"scenarios\":[".getBytes(StandardCharsets.UTF_8));
            for (int index = 0; index < scenarios.length(); index++) {
                if (index != 0) buffered.write(',');
                byte[] json;
                if (index < completedScenarioJson.size()) {
                    json = completedScenarioJson.get(index);
                } else {
                    JSONObject scenario = scenarios.getJSONObject(index);
                    json = scenario.toString().getBytes(StandardCharsets.UTF_8);
                    if (index == completedScenarioJson.size() && scenario.optBoolean("completed"))
                        completedScenarioJson.add(json);
                }
                buffered.write(json);
            }
            buffered.write(']');
            buffered.write('}');
            buffered.flush();
            output.getFD().sync();
        }
        if (!temporary.renameTo(new File(context.getFilesDir(), REPORT_FILE))) {
            throw new IOException("Cannot publish instrumentation report");
        }
    }
}
