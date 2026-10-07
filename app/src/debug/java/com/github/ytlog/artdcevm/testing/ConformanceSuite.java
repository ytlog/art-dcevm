package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Build;
import android.os.Process;
import android.util.Log;
import com.github.ytlog.artdcevm.internal.PatchRequest;
import com.github.ytlog.artdcevm.internal.PatchResult;
import com.github.ytlog.artdcevm.internal.RuntimeBackend;
import com.github.ytlog.artdcevm.engine.BackendRegistry;
import com.github.ytlog.artdcevm.fixture.Child;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.Peer;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONArray;
import org.json.JSONObject;

/** Runs inside the ordinary debug app process. Always start a fresh process for each run. */
public final class ConformanceSuite {
    private final Context context;
    private final JSONObject report = new JSONObject();
    private final JSONArray checks = new JSONArray();
    private int failures;
    private static volatile int sink;
    public ConformanceSuite(Context context) {
        this.context = context;
        System.loadLibrary("artdcevm_test_support");
    }

    public JSONObject run() {
        try {
            report.put("fingerprint", Build.FINGERPRINT).put("sdk", Build.VERSION.SDK_INT)
                    .put("uid", Process.myUid()).put("checks", checks).put("fullDcevmComplete", false);
            checkpoint();
            RuntimeBackend backend = BackendRegistry.open(context);
            report.put("capabilities", backend.capabilities());
            check("debug_agent_attached", backend.capabilities().getBoolean("attached"));
            boolean sourcePackaged = new File(context.getApplicationInfo().nativeLibraryDir,
                    "libartdcevm_source13.so").isFile();
            report.put("sourceModulePackaged", sourcePackaged);
            check("standalone_source_module_absent", !sourcePackaged);
            requirePatch("install_native_mapping_probe", backend.enableValidationHooks());
            checkPlan(backend, "body", 0, true);
            checkPlan(backend, "add", 5, true);
            runBodyAndAddition(backend);
            runHookRouting(backend);
            requirePatch("owned_evolution_initializes_without_source_module", backend.enableEvolution());
            check("owned_evolution_available", backend.capabilities().getBoolean("evolutionAvailable"));
            report.put("remaining", new JSONArray().put("hierarchy narrowing and removal")
                    .put("private/direct and abstract/concrete modifier transitions; exhaustive old-link visibility semantics")
                    .put("obsolete metadata reclamation"));
        } catch (Throwable error) {
            failures++;
            try { report.put("fatal", Log.getStackTraceString(error)); } catch (Exception ignored) { }
            Log.e("ArtDcevm", "Conformance failed", error);
        } finally {
            try { NativeTestBridge.releaseFixture(); } catch (Throwable ignored) { }
            try { report.put("failures", failures).put("completed", true); checkpoint(); } catch (Exception ignored) { }
        }
        return report;
    }

    private void runBodyAndAddition(RuntimeBackend backend) throws Exception {
        Evolving instance = new Evolving(41);
        Evolving alias = instance;
        Child child = new Child(10);
        Class<?> originalClass = instance.getClass();
        int hash = System.identityHashCode(instance);
        Method cachedMethod = originalClass.getMethod("compute");
        Field cachedField = originalClass.getField("value");
        Evolving.counter = 99;
        NativeTestBridge.retainFixture(instance);
        check("baseline", instance.compute() == 42 && child.compute() == 11);
        for (int i = 0; i < 100000; i++) sink = instance.compute();

        CountDownLatch entered = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        AtomicInteger oldResult = new AtomicInteger(-1);
        Thread oldFrame = new Thread(() -> {
            try { oldResult.set(instance.waitThenCompute(entered, release)); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        }, "obsolete-frame");
        oldFrame.start();
        try {
            if (!entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Old frame did not enter");
            requirePatch("body_install", apply(backend, PatchRequest.Operation.REDEFINE_BODY, "body"));
        } finally { release.countDown(); oldFrame.join(10000); }
        check("active_frame_completes_old_body", !oldFrame.isAlive() && oldResult.get() == 42);
        check("new_calls_use_new_body", instance.compute() == 43);
        check("cached_reflection_after_body", ((Integer) cachedMethod.invoke(instance)) == 43 && cachedField.getInt(instance) == 41);
        int[] jni = NativeTestBridge.inspectRetainedFixture();
        check("cached_jni_after_body", jni[0] == 43 && jni[1] == 41);

        // Keep a monitor held and a receiver in an active frame while ART migrates the object.
        CountDownLatch monitorEntered = new CountDownLatch(1);
        CountDownLatch monitorRelease = new CountDownLatch(1);
        AtomicInteger monitorResult = new AtomicInteger(-1);
        Thread monitorThread = new Thread(() -> {
            synchronized (instance) {
                monitorEntered.countDown();
                try { monitorRelease.await(); monitorResult.set(instance.compute()); }
                catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            }
        }, "migration-monitor");
        monitorThread.start();
        try {
            if (!monitorEntered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Monitor did not enter");
            requirePatch("add_fields_and_method_install", apply(backend, PatchRequest.Operation.REDEFINE_STRUCTURE, "add"));
        } finally { monitorRelease.countDown(); monitorThread.join(10000); }
        check("monitor_survives_migration", !monitorThread.isAlive() && monitorResult.get() == 44);
        check("object_identity_and_class_preserved", alias == instance && originalClass == instance.getClass() && System.identityHashCode(instance) == hash);
        check("existing_instance_state", instance.value == 41 && instance.compute() == 44);
        check("subclass_migrated", child.value == 10 && child.child == 90 && child.compute() == 13);
        Field added = originalClass.getField("added");
        check("new_field_default_for_existing_object", added.getLong(instance) == 0);
        added.setLong(instance, 9);
        check("new_method_visible_in_reflection", ((Integer) originalClass.getMethod("addedMethod").invoke(instance)) == 150);
        check("static_state_preserved_no_clinit_rerun", Evolving.counter == 99 && originalClass.getField("generation").getInt(null) == 0);
        check("cached_reflection_after_structure", ((Integer) cachedMethod.invoke(instance)) == 44 && cachedField.getInt(instance) == 41);
        jni = NativeTestBridge.inspectRetainedFixture();
        check("cached_jni_after_structure", jni[0] == 44 && jni[1] == 41);
        Evolving fresh = new Evolving(4);
        check("new_instance_uses_new_constructor", fresh.compute() == 7 && added.getLong(fresh) == 77);
        System.gc();
        System.runFinalization();
        check("state_after_gc", instance.value == 41 && added.getLong(instance) == 9 && ((Integer) cachedMethod.invoke(instance)) == 44);

        // These plans must use the evolved live class, not the class's original APK schema.
        checkPlan(backend, "add2", 0, true);
        checkPlan(backend, "delete", 10, true);
        checkPlan(backend, "types", 16 | 32, false);
        checkPlan(backend, "hierarchy", 64 | 128, false);
        check("schema_analysis_does_not_mutate_live_state", instance.compute() == 44 && added.getLong(instance) == 9 && Evolving.counter == 99);
        byte[] sameDex = readAsset("add2/com/github/ytlog/artdcevm/fixture/Evolving.dex");
        JSONObject duplicate = backend.analyze(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA,
                new Class<?>[]{Evolving.class, Evolving.class}, new byte[][]{sameDex, sameDex}));
        check("duplicate_class_batch_rejected_by_planner", duplicate.getInt("code") == 103 && !duplicate.has("plan"));
        JSONObject malformed = backend.analyze(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA,
                new Class<?>[]{Evolving.class}, new byte[][]{new byte[]{1, 2, 3, 4}}));
        check("malformed_dex_rejected_by_planner", malformed.getInt("code") == 60 && !malformed.has("plan"));

        Peer peer = new Peer();
        PatchResult invalidBatch = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_BODY,
                new Class<?>[]{Evolving.class, Peer.class},
                new byte[][]{readAsset("add2/com/github/ytlog/artdcevm/fixture/Evolving.dex"), new byte[]{1, 2, 3, 4}}));
        report.put("invalidBatchResult", invalidBatch.toString());
        check("invalid_second_class_does_not_publish_first", !invalidBatch.installed() && instance.compute() == 44 && peer.compute() == 11);

        PatchResult deletion = apply(backend, PatchRequest.Operation.REDEFINE_STRUCTURE, "delete");
        report.put("stockArtDeletionResult", deletion.toString());
        check("stock_art_rejects_deletion_atomically", !deletion.installed() && instance.compute() == 44 && added.getLong(instance) == 9);
        PatchResult evolution = apply(backend, PatchRequest.Operation.EVOLVE_SCHEMA, "delete");
        report.put("customEvolutionResult", evolution.toString());
        check("uninitialized_evolution_is_explicit", evolution.code == 98);
    }

    private PatchResult apply(RuntimeBackend backend, PatchRequest.Operation operation, String asset) throws Exception {
        return backend.apply(new PatchRequest(operation, new Class<?>[]{Evolving.class}, new byte[][]{readAsset(asset + "/com/github/ytlog/artdcevm/fixture/Evolving.dex")}));
    }
    private void checkPlan(RuntimeBackend backend, String variant, int expectedFeatures, boolean exact) throws Exception {
        JSONObject analysis = backend.analyze(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA,
                new Class<?>[]{Evolving.class}, new byte[][]{readAsset(variant + "/com/github/ytlog/artdcevm/fixture/Evolving.dex")}));
        if (analysis.getInt("code") != 0) throw new AssertionError("Schema analysis: " + analysis);
        JSONObject plan = analysis.getJSONObject("plan");
        int features = plan.getInt("requiredFeatures");
        JSONArray plans = report.optJSONArray("migrationPlans");
        if (plans == null) { plans = new JSONArray(); report.put("migrationPlans", plans); }
        plans.put(new JSONObject().put("variant", variant).put("plan", plan));
        check("plan_" + variant, exact ? features == expectedFeatures : (features & expectedFeatures) == expectedFeatures,
                "features=" + features);
    }
    private void runHookRouting(RuntimeBackend backend) throws Exception {
        requirePatch("install_pinned_native_hooks", backend.enableValidationHooks());
        long fields = backend.capabilities().getLong("hookFieldCalls");
        long methods = backend.capabilities().getLong("hookMethodCalls");
        requirePatch("redefine_through_owned_native_code", apply(backend, PatchRequest.Operation.REDEFINE_BODY, "add2"));
        JSONObject capabilities = backend.capabilities();
        report.put("capabilitiesAfterHooks", capabilities);
        check("art_field_validation_called_owned_code", capabilities.getLong("hookFieldCalls") > fields);
        check("art_method_validation_called_owned_code", capabilities.getLong("hookMethodCalls") > methods);
        check("art_member_mapping_called_owned_code", capabilities.getLong("hookMappingCalls") > 0);
        check("hook_preserves_runtime_semantics", new Evolving(1).compute() == 6);
    }
    private byte[] readAsset(String name) throws Exception {
        try (InputStream input = context.getAssets().open(name); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192]; int n;
            while ((n = input.read(buffer)) != -1) output.write(buffer, 0, n);
            return output.toByteArray();
        }
    }
    private void requirePatch(String name, PatchResult result) throws Exception {
        check(name, result.installed(), result.toString());
        if (!result.installed()) throw new AssertionError(name + ": " + result);
    }
    private void check(String name, boolean passed) throws Exception { check(name, passed, ""); }
    private void check(String name, boolean passed, String detail) throws Exception {
        if (!passed) failures++;
        checks.put(new JSONObject().put("name", name).put("passed", passed).put("detail", detail));
        Log.i("ArtDcevm", name + "=" + passed + " " + detail);
        checkpoint();
    }
    private void checkpoint() throws Exception {
        File target = new File(context.getFilesDir(), "conformance.json");
        try (FileOutputStream out = new FileOutputStream(target)) { out.write(report.toString(2).getBytes(StandardCharsets.UTF_8)); }
    }
}
