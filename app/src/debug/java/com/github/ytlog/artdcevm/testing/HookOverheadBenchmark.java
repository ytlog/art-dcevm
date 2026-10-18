package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.HookBenchTypes;
import com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Steady-state cost of the engine's resident hooks, one state per process:
 * <ul>
 *   <li>{@code none}: no JVMTI agent;</li>
 *   <li>{@code agent}: the engine library attached as a JVMTI agent exactly as
 *       {@link DcevmRuntime#initialize} attaches it, without installing hooks:
 *       ART's own cost of a JVMTI environment;</li>
 *   <li>{@code engine}: {@link DcevmRuntime#initialize}, no redefinition;</li>
 *   <li>{@code engine-client}: engine and the standard JVMTI client, whose
 *       environment holds can_redefine_classes, no redefinition;</li>
 *   <li>{@code added}: as engine-client, after a structural redefinition that
 *       adds members to an unrelated class (control for ART's own
 *       after-effects of a structural transaction);</li>
 *   <li>{@code removed}: as engine-client, after one class dropped an
 *       interface, which enables process-wide IMT sealing for classes linked
 *       later;</li>
 *   <li>controls without the engine's transaction: {@code art-body} (client
 *       only, standard RedefineClasses of a method body), {@code art-added}
 *       (client only, ART's structural extension adding members) and
 *       {@code engine-body} (engine-client, a method body change).</li>
 * </ul>
 * {@code mode=interp} stops JIT compilation before anything is measured;
 * {@code mode=jit} warms each workload until it runs compiled. Early receivers
 * are linked before the state is selected and late ones after it.
 * ART 13 only: append {@code -observe}, {@code -restore}, or {@code -jvmti}
 * to {@code art-added} or {@code added} for the instrumentation experiment.
 * A completed benchmark does not imply restoration succeeded; the probe
 * records the remaining blockers and {@code restorationSucceeded} separately.
 */
public final class HookOverheadBenchmark extends Instrumentation {
    private static final String REPORT_FILE = "hook-overhead.json";
    private static final int SAMPLES = 21;
    private static final String LOAD_PREFIX = "com.github.ytlog.artdcevm.fixture.HookBenchLoadTypes$K";
    private static final String LATE_PREFIX = "com.github.ytlog.artdcevm.fixture.HookBenchLateTypes$L";

    private interface Workload { long run(int count) throws Throwable; }

    private Bundle arguments;
    private Context context;
    private Throwable preparationError;
    private final JSONObject report = new JSONObject();

    /** Stops ART's JIT workers for the rest of the process (test engine library). */
    static native int suspendJit();
    static native boolean jniPrepare(Class<?> base);
    static native long jniLoop(Object target, int kind, int count);

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        this.arguments = arguments;
        start();
    }

    @Override public void onStart() {
        context = getTargetContext();
        if (argument("probeunwind", "false").equals("true")
                && argument("state", "none").matches(".+-(observe|restore|jvmti)")) {
            // Attaching before the new Thread starts removes boot-image compiled
            // entry points. Let the original InstrumentationThread's old quick
            // frames return completely before redefining on this new thread.
            try {
                InterfaceAbortInstrumentation.configureRuntime(arguments);
                StandardJvmtiClient.attach(context);
            } catch (Throwable error) { preparationError = error; }
            Thread creator = Thread.currentThread();
            new Thread(() -> {
                try { creator.join(); }
                catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    preparationError = error;
                }
                executeBenchmark();
            }, "jit-probe-worker").start();
            return;
        }
        executeBenchmark();
    }

    private void executeBenchmark() {
        boolean completed = false;
        Bundle result = new Bundle();
        try {
            if (preparationError != null) throw preparationError;
            run();
            completed = true;
        } catch (Throwable error) {
            try { report.put("fatalError", error.toString()); } catch (Exception ignored) { }
        }
        try {
            report.put("completed", completed);
            writeReport();
        } catch (Exception error) {
            completed = false;
            result.putString("reportError", error.toString());
        }
        result.putString("reportFile", "files/" + REPORT_FILE);
        result.putBoolean("completed", completed);
        finish(completed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }

    private void run() throws Throwable {
        String state = argument("state", "none");
        String baseState = state.replaceFirst("-(observe|restore|jvmti)$", "");
        String probe = state.equals(baseState) ? "none" : state.substring(baseState.length() + 1);
        String mode = argument("mode", "jit");
        if (!Arrays.asList("none", "agent", "engine", "engine-client", "added", "removed",
                "art-body", "art-added", "engine-body").contains(baseState)
                || (!probe.equals("none") && !Arrays.asList("art-added", "added").contains(baseState)))
            throw new IllegalArgumentException("Unsupported state " + state);
        if (!mode.equals("jit") && !mode.equals("interp"))
            throw new IllegalArgumentException("Unsupported mode " + mode);
        report.put("schemaVersion", 1).put("state", state).put("mode", mode)
                .put("device", new JSONObject().put("model", Build.MODEL)
                        .put("api", Build.VERSION.SDK_INT).put("release", Build.VERSION.RELEASE));
        // Selects CheckJNI before any agent; this only loads the engine library.
        InterfaceAbortInstrumentation.configureRuntime(arguments);
        report.put("checkJni", arguments == null ? null : arguments.getString("checkjni"));
        System.loadLibrary("artdcevm_bench");
        if (!jniPrepare(HookBenchTypes.Base.class)) throw new AssertionError("JNI IDs unavailable");

        HookBenchTypes.Base[] early = {new HookBenchTypes.E0(), new HookBenchTypes.E1(),
                new HookBenchTypes.E2(), new HookBenchTypes.E3(), new HookBenchTypes.E4(),
                new HookBenchTypes.E5(), new HookBenchTypes.E6(), new HookBenchTypes.E7()};
        HookBenchTypes.Shape[] earlyShapes = new HookBenchTypes.Shape[8];
        for (int i = 0; i < 8; i++) earlyShapes[i] = (HookBenchTypes.Shape) early[i];
        InterfaceRemovalFixture.Subject subject = new InterfaceRemovalFixture.Subject();
        Evolving evolving = new Evolving(10);

        JSONObject setup = new JSONObject();
        report.put("setup", setup);
        boolean engine = !Arrays.asList("none", "agent", "art-body", "art-added").contains(baseState);
        boolean client = !Arrays.asList("none", "agent", "engine").contains(baseState);
        if (baseState.equals("agent")) {
            System.loadLibrary("artdcevm");
            Debug.attachJvmtiAgent("libartdcevm.so", null, context.getApplicationContext().getClassLoader());
        }
        if (engine) {
            long begin = System.nanoTime();
            DcevmRuntime.initialize(context);
            setup.put("initializeNanos", System.nanoTime() - begin);
        }
        if (client) StandardJvmtiClient.attach(context);
        if (!probe.equals("none")) {
            JitInstrumentationProbe.load();
            int code = JitInstrumentationProbe.install();
            if (code != 0) throw new AssertionError("Cannot install ART 13 probe: " + code);
            if (probe.equals("jvmti")) JitInstrumentationProbe.attach(context);
        }
        if (client && !baseState.equals("engine-client")) {
            boolean removal = baseState.equals("removed");
            boolean body = baseState.endsWith("-body");
            Class<?> type = removal ? InterfaceRemovalFixture.Subject.class : Evolving.class;
            String path = (removal ? "interface-remove/" : body ? "body/" : "add/")
                    + type.getName().replace('.', '/') + ".dex";
            Class<?>[] classes = {type};
            byte[][] dexes = {readAsset(path)};
            long begin = System.nanoTime();
            int code = baseState.equals("art-added") ? StandardJvmtiClient.redefineStructurally(classes, dexes)
                    : StandardJvmtiClient.redefine(classes, dexes);
            setup.put("redefineNanos", System.nanoTime() - begin).put("redefineCode", code);
            if (code != 0) throw new AssertionError("Redefinition failed: " + code);
            if (removal ? InterfaceRemovalFixture.Removed.class.isInstance(subject)
                        : evolving.compute() != (body ? 12 : 13))
                throw new AssertionError("Redefinition not visible");
        }
        if (!probe.equals("none")) {
            JSONObject diagnostics = new JSONObject().put("strategy", probe);
            report.put("instrumentationProbe", diagnostics);
            diagnostics.put("afterRedefine", JitInstrumentationProbe.state());
            diagnostics.put("unwindOriginalThread", argument("probeunwind", "false"));
            if (argument("probeunwind", "false").equals("true")) {
                CountDownLatch mainProgress = new CountDownLatch(1);
                new Handler(Looper.getMainLooper()).post(mainProgress::countDown);
                if (!mainProgress.await(5, TimeUnit.SECONDS))
                    throw new AssertionError("Main Looper did not advance");
            }
            if (!probe.equals("observe")) {
                JSONArray restoration = new JSONArray();
                diagnostics.put("restoration", restoration);
                for (int attempt = 0; attempt < 10; attempt++) {
                    long begin = System.nanoTime();
                    int code = probe.equals("restore") ? JitInstrumentationProbe.maybeRestore()
                            : JitInstrumentationProbe.cycleJvmti();
                    long elapsed = System.nanoTime() - begin;
                    JSONObject status = JitInstrumentationProbe.state();
                    restoration.put(new JSONObject().put("code", code)
                            .put("nanos", elapsed).put("state", status));
                    if (code < 0 || (probe.equals("jvmti") && code != 0))
                        throw new AssertionError("Restoration rejected: " + code);
                    if (status.getLong("exitStubsInstalled") == 0) break;
                    Thread.sleep(50);
                }
            }
            diagnostics.put("beforeWorkloads", JitInstrumentationProbe.state());
            if (!probe.equals("observe")) diagnostics.put("restorationSucceeded",
                    diagnostics.getJSONObject("beforeWorkloads").getLong("exitStubsInstalled") == 0);
        }
        if (engine) setup.put("capabilities", DcevmRuntime.capabilities());
        // After setup: ART's Redefiner suspends the JIT itself and restarts it.
        // No benchmark method has run yet.
        if (mode.equals("interp")) {
            System.loadLibrary("artdcevm");
            int code = suspendJit();
            if (code != 0) throw new AssertionError("Cannot suspend the JIT: " + code);
            report.put("jitSuspended", true);
        }

        // Linked after the state is selected. Only JVMTI can ask without loading:
        // ART's findLoadedClass also defines classes from the loader's DEX files.
        ClassLoader loader = context.getClassLoader();
        if (client) {
            setup.put("lateLoadedBefore", StandardJvmtiClient.isLoaded(signature(LATE_PREFIX + "0")));
            setup.put("loadTypesLoadedBefore", StandardJvmtiClient.isLoaded(signature(LOAD_PREFIX + "000")));
        }
        HookBenchTypes.Shape[] lateShapes = new HookBenchTypes.Shape[8];
        for (int i = 0; i < 8; i++)
            lateShapes[i] = (HookBenchTypes.Shape) Class.forName(LATE_PREFIX + i, true, loader)
                    .getDeclaredConstructor().newInstance();

        JSONObject once = new JSONObject();
        report.put("once", once);
        Class<?>[] loadedTypes = new Class<?>[128];
        long begin = System.nanoTime();
        for (int i = 0; i < loadedTypes.length; i++)
            loadedTypes[i] = Class.forName(LOAD_PREFIX + String.format(Locale.ROOT, "%03d", i), false, loader);
        once.put("loadLinkNanosPerClass", (System.nanoTime() - begin) / loadedTypes.length);
        begin = System.nanoTime();
        long sum = 0;
        for (Class<?> type : loadedTypes)
            sum += ((HookBenchTypes.Shape) type.getDeclaredConstructor().newInstance()).area(3);
        once.put("initInstantiateCallNanosPerClass", (System.nanoTime() - begin) / loadedTypes.length);
        once.put("checksum", sum);

        HookBenchTypes.Base target = early[0];
        HookBenchTypes.Shape shape = earlyShapes[0];
        HookBenchTypes.Shape lateShape = lateShapes[0];
        Method reflected = HookBenchTypes.E0.class.getMethod("value", int.class);
        Constructor<HookBenchTypes.E0> constructor = HookBenchTypes.E0.class.getConstructor();
        Object[] reflectedArgs = {3};
        String forName = HookBenchTypes.E0.class.getName();
        boolean compiled = mode.equals("jit");
        JSONObject workloads = new JSONObject();
        report.put("workloads", workloads);
        measure(workloads, "virtualMono", compiled, n -> HookBenchTypes.virtualMono(target, n));
        measure(workloads, "virtualMega", compiled, n -> HookBenchTypes.virtualMega(early, n));
        measure(workloads, "interfaceMono", compiled, n -> HookBenchTypes.interfaceMono(shape, n));
        measure(workloads, "interfaceMega", compiled, n -> HookBenchTypes.interfaceMega(earlyShapes, n));
        measure(workloads, "lateInterfaceMono", compiled, n -> HookBenchTypes.lateInterfaceMono(lateShape, n));
        measure(workloads, "lateInterfaceMega", compiled, n -> HookBenchTypes.lateInterfaceMega(lateShapes, n));
        measure(workloads, "reflectInvoke", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; i++) total += (Integer) reflected.invoke(target, reflectedArgs);
            return total;
        });
        measure(workloads, "reflectNewInstance", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; i++) total += constructor.newInstance().field;
            return total;
        });
        measure(workloads, "jniCallIntMethod", compiled, n -> jniLoop(target, 0, n));
        measure(workloads, "jniCallStaticIntMethod", compiled, n -> jniLoop(target, 1, n));
        measure(workloads, "jniGetIntField", compiled, n -> jniLoop(target, 2, n));
        measure(workloads, "classForNameLoaded", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; i++) total += Class.forName(forName, false, loader).hashCode() & 1;
            return total;
        });
        if (!probe.equals("none")) report.getJSONObject("instrumentationProbe")
                .put("afterWorkloads", JitInstrumentationProbe.state());
    }

    /**
     * Sizes one batch to about 5 ms, warms the workload (for JIT: 400 ms, a
     * pause for the compiler thread, then 200 ms), and records ns per call
     * over {@link #SAMPLES} batches.
     */
    private void measure(JSONObject into, String name, boolean compiled, Workload workload) throws Throwable {
        int count = 64;
        while (true) {
            long begin = System.nanoTime();
            workload.run(count);
            if (System.nanoTime() - begin >= 5_000_000L || count >= (1 << 26)) break;
            count *= 2;
        }
        if (compiled) {
            warm(workload, count, 400_000_000L);
            Thread.sleep(100);
            warm(workload, count, 200_000_000L);
        } else {
            for (int i = 0; i < 3; i++) workload.run(count);
        }
        double[] samples = new double[SAMPLES];
        long checksum = 0;
        for (int i = 0; i < SAMPLES; i++) {
            long begin = System.nanoTime();
            checksum += workload.run(count);
            samples[i] = (System.nanoTime() - begin) / (double) count;
        }
        double[] sorted = samples.clone();
        Arrays.sort(sorted);
        JSONArray raw = new JSONArray();
        for (double sample : samples) raw.put(Math.round(sample * 1000) / 1000.0);
        into.put(name, new JSONObject().put("callsPerBatch", count)
                .put("medianNs", sorted[SAMPLES / 2]).put("minNs", sorted[0])
                .put("p10Ns", sorted[SAMPLES / 10]).put("p90Ns", sorted[SAMPLES - 1 - SAMPLES / 10])
                .put("samplesNs", raw).put("checksum", checksum));
        writeReport();
    }

    private static void warm(Workload workload, int count, long nanos) throws Throwable {
        long end = System.nanoTime() + nanos;
        while (System.nanoTime() < end) workload.run(count);
    }

    private static String signature(String name) {
        return "L" + name.replace('.', '/') + ";";
    }

    private String argument(String key, String fallback) {
        String value = arguments == null ? null : arguments.getString(key);
        return value == null ? fallback : value;
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

    private void writeReport() throws Exception {
        File temporary = new File(context.getFilesDir(), REPORT_FILE + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary)) {
            output.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
        if (!temporary.renameTo(new File(context.getFilesDir(), REPORT_FILE)))
            throw new IOException("Cannot publish benchmark report");
    }
}
