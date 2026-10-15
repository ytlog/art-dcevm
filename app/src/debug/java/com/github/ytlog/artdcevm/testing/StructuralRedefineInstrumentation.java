package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
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
import com.github.ytlog.artdcevm.fixture.StorageCaller;
import com.github.ytlog.artdcevm.fixture.StorageSubject;
import com.github.ytlog.artdcevm.fixture.SuperChild;
import com.github.ytlog.artdcevm.fixture.SuperMiddle;
import com.github.ytlog.artdcevm.fixture.SuperSubject;
import com.github.ytlog.artdcevm.fixture.ToolHost;
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
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.IntSupplier;

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
        runCopiedDefaultScenario();
        runInterfaceScenarios();
        runSuperclassScenarios();
        runPreloadScenarios();
        runToolScenarios();
        runFieldScenarios(fields, originalFieldSchema, originalFieldValues,
                originalObject, originalStaticObject);
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

    // A loaded subclass owns a copied interface default until its superclass
    // gains a public implementation; the re-linked subclass then has no copy.
    private void runCopiedDefaultScenario() throws Exception {
        CopiedChild child = new CopiedChild();
        CopiedDefault viaInterface = child;
        // Stage 1 admits the interface and stops safely before structural allocation.
        runScenario("interface-default-body", 98, scenario -> {
            scenario.redefine(new Class<?>[]{CopiedDefault.class}, "interface-default-body");
            scenario.check("interfaceReason", true, DcevmRuntime.capabilities()
                    .optString("lastRejection").contains("JVMTI_ERROR_NOT_AVAILABLE"));
            scenario.check("copiedUnchanged", 91, viaInterface.copiedValue());
        });
        runScenario("copied-default-add", 0, scenario -> {
            scenario.check("copiedBefore", 91, viaInterface.copiedValue());
            scenario.redefine(new Class<?>[]{CopiedParent.class}, "copied-default-add");
            scenario.check("oldChildInterface", 92, viaInterface.copiedValue());
            scenario.check("oldChildVirtual", 92, child.copiedValue());
            scenario.check("newChild", 92, new CopiedChild().copiedValue());
            scenario.check("parentValue", 90, child.parentValue());
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
        runScenario("interface-remove", 66, scenario -> {
            scenario.redefine(new Class<?>[]{InterfaceSubject.class}, "interface-original");
            checkAddedInterfaces(scenario, subject, child, interfaces);
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

    // Verified callers resolve a method by name and signature without checking
    // its invoke kind, so no change may make a method of that identity static
    // where it was an instance method (or the reverse), or private where it was virtual.
    private void runMethodKindChangeScenarios() throws Exception {
        AccessSubject access = new AccessSubject();
        runScenario("method-private", 71, scenario -> {
            scenario.check("callerBefore", 7, AccessCaller.callTarget(access));
            scenario.redefine(new Class<?>[]{AccessSubject.class}, "method-private");
            scenario.check("targetIsPublic", true, Modifier.isPublic(
                    AccessSubject.class.getDeclaredMethod("target").getModifiers()));
            scenario.check("callerAfter", 7, AccessCaller.callTarget(access));
        });
        StorageSubject subject = new StorageSubject();
        Method cachedStatic = StorageSubject.class.getDeclaredMethod("staticValue");
        runScenario("method-storage", 71, scenario -> {
            scenario.check("callerStaticBefore", 32, StorageCaller.callStatic());
            scenario.check("callerVirtualBefore", 8, StorageCaller.callVirtual(subject));
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage");
            scenario.check("staticValueIsStatic", true, Modifier.isStatic(
                    StorageSubject.class.getDeclaredMethod("staticValue").getModifiers()));
            scenario.check("callerStatic", 32, StorageCaller.callStatic());
            scenario.check("callerVirtual", 8, StorageCaller.callVirtual(subject));
            scenario.check("combined", 40, subject.combined());
        });
        runScenario("method-storage-delete", 0, scenario -> {
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage-delete");
            scenario.check("combined", 35, subject.combined());
            scenario.expectError("callerStatic", NoSuchMethodError.class, StorageCaller::callStatic);
            scenario.expectError("callerVirtual", NoSuchMethodError.class,
                    () -> StorageCaller.callVirtual(subject));
            scenario.expectReflectionError("cachedReflectionStatic", () -> cachedStatic.invoke(null));
        });
        // The deleted methods come back only as the kinds their callers were verified against.
        runScenario("method-storage-readd", 71, scenario -> {
            scenario.redefine(new Class<?>[]{StorageSubject.class}, "method-storage");
            scenario.check("combined", 35, subject.combined());
            scenario.expectError("callerStatic", NoSuchMethodError.class, StorageCaller::callStatic);
            scenario.expectError("callerVirtual", NoSuchMethodError.class,
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
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
        if (!temporary.renameTo(new File(context.getFilesDir(), REPORT_FILE))) {
            throw new IOException("Cannot publish instrumentation report");
        }
    }
}
