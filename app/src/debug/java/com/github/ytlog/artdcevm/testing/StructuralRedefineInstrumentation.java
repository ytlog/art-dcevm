package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.ClassFinalSubject;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.FieldSubject;
import com.github.ytlog.artdcevm.fixture.FinalFieldSubject;
import com.github.ytlog.artdcevm.fixture.ModifierPassive;
import com.github.ytlog.artdcevm.fixture.ModifierSubject;

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
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Runs Phase 1/2 conformance through a separate agent's standard JVMTI entry point. */
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
        runScenario("method-delete", 67, scenario -> {
            scenario.redefine(new Class<?>[]{Evolving.class}, "method-delete");
            scenario.check("unchangedMembers", addedSchema, memberSchema(Evolving.class));
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
            writeReport(false);
        }

        void check(String name, Object expected, Object actual) throws Exception {
            boolean matches = expected == null ? actual == null : expected.equals(actual);
            passed &= matches;
            checks.put(new JSONObject().put("name", name).put("expected", JSONObject.wrap(expected))
                    .put("actual", JSONObject.wrap(actual)).put("passed", matches));
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
