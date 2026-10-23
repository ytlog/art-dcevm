package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.app.KeyguardManager;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;

import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.HookBenchTypes;
import com.github.ytlog.artdcevm.fixture.InterfaceEvolutionFixture;
import com.github.ytlog.artdcevm.fixture.InterfaceRemovalFixture;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
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
 * {@code mode=jit} warms workloads with JIT enabled; it does not certify each
 * workload lambda compiled. Early receivers
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
    private int benchmarkCpu = -1;
    private boolean conditioned;
    private String clockUnits;
    private HookBenchmarkActivity foregroundActivity;
    private int initialForegroundEpoch = -1;
    private final JSONObject report = new JSONObject();

    /** Stops ART's JIT workers for the rest of the process (test engine library). */
    static native int suspendJit();
    private static native void warmNative(Object workload, int count, long nanos);
    private static native long referenceWork(int expectedCpu);
    static native int boundHandleEntryMode();
    static native int boundHandlePreparationProofs();
    static native boolean jniPrepare(Class<?> base);
    static native int pinCpu(int cpu);
    static native int pinnedCpu();
    static native long jniLoop(Object target, int kind, int count);
    static native long interpreterUnwind();
    private static long unwindFromInterpretedCaller() { return interpreterUnwind(); }

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
            if (argument("foreground", "false").equals("true")) startForeground();
            run();
            completed = true;
        } catch (Throwable error) {
            try { report.put("fatalError", error.toString()); } catch (Exception ignored) { }
        } finally {
            try {
                if (foregroundActivity != null) runOnMainBounded(foregroundActivity::finish);
            } catch (Throwable error) {
                completed = false;
                try { report.put("foregroundCleanupError", error.toString()); } catch (Exception ignored) { }
            }
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

    private void startForeground() throws Exception {
        report.put("foreground", true);
        KeyguardManager keyguard = (KeyguardManager) context.getSystemService(Context.KEYGUARD_SERVICE);
        PowerManager power = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        if (keyguard == null || power == null || keyguard.isKeyguardLocked() || !power.isInteractive())
            throw new AssertionError("Foreground benchmark requires an unlocked, interactive device");
        ActivityMonitor monitor = addMonitor(HookBenchmarkActivity.class.getName(), null, false);
        try {
            // The host explicitly launches the inert Debug window through ADB.
            // Vendor background-start policy can reject an in-app launch even
            // with an unlocked screen and active instrumentation.
            report.put("foregroundLaunchRequested", true);
            writeReport();
            Activity activity = monitor.waitForActivityWithTimeout(15000);
            if (!(activity instanceof HookBenchmarkActivity))
                throw new AssertionError("Foreground benchmark activity did not start");
            foregroundActivity = (HookBenchmarkActivity) activity;
            if (!foregroundActivity.awaitReady()) throw new AssertionError("Foreground window did not gain focus");
            initialForegroundEpoch = foregroundActivity.interruptions();
            long until = System.nanoTime() + 5_000_000_000L;
            while (!threadGroup().equals("/top-app") && System.nanoTime() < until) Thread.sleep(20);
            checkForeground();
            report.put("foregroundReady", true).put("foregroundCpuset", "/top-app")
                    .put("foregroundEpoch", initialForegroundEpoch);
        } finally {
            removeMonitor(monitor);
            if (foregroundActivity == null && monitor.getLastActivity() instanceof HookBenchmarkActivity)
                runOnMainBounded(monitor.getLastActivity()::finish);
        }
    }

    // Instrumentation.runOnMainSync has no deadline if the main looper stalls.
    // Bound startup and cleanup too, without launching a replacement process.
    private void runOnMainBounded(Runnable action) throws InterruptedException {
        Handler handler = new Handler(Looper.getMainLooper());
        CountDownLatch done = new CountDownLatch(1);
        Throwable[] failure = new Throwable[1];
        Runnable task = () -> {
            try { action.run(); }
            catch (Throwable error) { failure[0] = error; }
            finally { done.countDown(); }
        };
        if (!handler.post(task)) throw new AssertionError("Benchmark main looper rejected action");
        try {
            if (!done.await(5, TimeUnit.SECONDS))
                throw new AssertionError("Benchmark main-thread action timed out");
        } finally {
            handler.removeCallbacks(task);
        }
        if (failure[0] != null) throw new AssertionError("Benchmark main-thread action failed", failure[0]);
    }

    private int checkForeground() throws Exception {
        if (foregroundActivity == null) return -1;
        if (!foregroundActivity.isReady() || !threadGroup().equals("/top-app") ||
                foregroundActivity.interruptions() != initialForegroundEpoch)
            throw new AssertionError("Foreground benchmark lost focus or top-app scheduling group");
        return initialForegroundEpoch;
    }

    private String threadGroup() throws IOException {
        try (InputStream input = new java.io.FileInputStream("/proc/thread-self/cpuset");
             ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[256];
            for (int n; (n = input.read(buffer)) != -1;) bytes.write(buffer, 0, n);
            return bytes.toString("UTF-8").trim();
        }
    }

    private void run() throws Throwable {
        String state = argument("state", "none");
        String baseState = state.replaceFirst("-(observe|restore|jvmti)$", "");
        String probe = state.equals(baseState) ? "none" : state.substring(baseState.length() + 1);
        String mode = argument("mode", "jit");
        if (!Arrays.asList("none", "agent", "engine", "engine-client", "added", "removed", "selected",
                "art-body", "art-added", "engine-body").contains(baseState)
                || (!probe.equals("none") && !Arrays.asList("art-added", "added").contains(baseState)))
            throw new IllegalArgumentException("Unsupported state " + state);
        if (!mode.equals("jit") && !mode.equals("interp"))
            throw new IllegalArgumentException("Unsupported mode " + mode);
        conditioned = argument("steadyConditioning", "false").equals("true");
        clockUnits = argument("clockUnits", "");
        if (!clockUnits.isEmpty()) {
            for (String unit : clockUnits.split(",", -1))
                if (!unit.equals("methodHandleBound") && !unit.equals("reflectNewInstance"))
                    throw new IllegalArgumentException("Unsupported clock unit");
        }
        report.put("schemaVersion", 6).put("clockUnits", clockUnits)
                .put("measurementProtocol", conditioned
                ? "entry-and-batch-conditioning-v4" : "entry-warm-v1")
                .put("state", state).put("mode", mode)
                .put("device", new JSONObject().put("model", Build.MODEL)
                        .put("api", Build.VERSION.SDK_INT).put("release", Build.VERSION.RELEASE));
        // Selects CheckJNI before any agent; this only loads the engine library.
        InterfaceAbortInstrumentation.configureRuntime(arguments);
        report.put("checkJni", arguments == null ? null : arguments.getString("checkjni"));
        System.loadLibrary("artdcevm_bench");
        if (!jniPrepare(HookBenchTypes.Base.class)) throw new AssertionError("JNI IDs unavailable");
        String cpuArgument = argument("cpu", "none");
        if (!cpuArgument.equals("none")) {
            benchmarkCpu = Integer.parseInt(cpuArgument);
            if (benchmarkCpu < 0 || pinCpu(benchmarkCpu) != benchmarkCpu)
                throw new AssertionError("Cannot bind benchmark CPU " + benchmarkCpu);
        }
        report.put("cpuAffinity", benchmarkCpu);

        HookBenchTypes.Base[] early = {new HookBenchTypes.E0(), new HookBenchTypes.E1(),
                new HookBenchTypes.E2(), new HookBenchTypes.E3(), new HookBenchTypes.E4(),
                new HookBenchTypes.E5(), new HookBenchTypes.E6(), new HookBenchTypes.E7()};
        HookBenchTypes.Shape[] earlyShapes = new HookBenchTypes.Shape[8];
        for (int i = 0; i < 8; i++) earlyShapes[i] = (HookBenchTypes.Shape) early[i];
        InterfaceRemovalFixture.Subject subject = new InterfaceRemovalFixture.Subject();
        Object selectionReceiver = baseState.equals("selected")
                ? new InterfaceEvolutionFixture.ProtectedImpl() : null;
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
            boolean selection = baseState.equals("selected");
            boolean body = baseState.endsWith("-body");
            Class<?> type = selection ? InterfaceEvolutionFixture.ProtectedShadow.class
                    : removal ? InterfaceRemovalFixture.Subject.class : Evolving.class;
            String path = (selection ? "interface-shadow-protected/" : removal ? "interface-remove/" : body ? "body/" : "add/")
                    + type.getName().replace('.', '/') + ".dex";
            Class<?>[] classes = {type};
            byte[][] dexes = {readAsset(path)};
            long begin = System.nanoTime();
            int code = baseState.equals("art-added") ? StandardJvmtiClient.redefineStructurally(classes, dexes)
                    : StandardJvmtiClient.redefine(classes, dexes);
            setup.put("redefineNanos", System.nanoTime() - begin).put("redefineCode", code);
            if (code != 0) throw new AssertionError("Redefinition failed: " + code);
            if (selection) {
                try {
                    type.getMethod("shadow").invoke(selectionReceiver);
                    throw new AssertionError("Interface selection did not install its error target");
                } catch (java.lang.reflect.InvocationTargetException expected) {
                    if (expected.getCause().getClass() != IllegalAccessError.class) throw expected;
                }
                setup.put("interfaceSelectionActive", true);
            } else if (removal ? InterfaceRemovalFixture.Removed.class.isInstance(subject)
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
        setup.put("boundHandleEntryMode", boundHandleEntryMode());
        setup.put("boundHandlePreparationProofs", boundHandlePreparationProofs());
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
        measure(workloads, "privateDirect", compiled, HookBenchTypes::privateDirect);
        JSONObject direct = workloads.getJSONObject("privateDirect");
        long directCalls = direct.getInt("callsPerBatch");
        if (direct.getLong("checksum") != directCalls * (directCalls + 1) / 2 * SAMPLES)
            throw new AssertionError("Invalid private direct benchmark checksum");
        measure(workloads, "virtualMono", compiled, n -> HookBenchTypes.virtualMono(target, n));
        measure(workloads, "virtualMega", compiled, n -> HookBenchTypes.virtualMega(early, n));
        measure(workloads, "interfaceMono", compiled, n -> HookBenchTypes.interfaceMono(shape, n));
        measure(workloads, "interfaceMega", compiled, n -> HookBenchTypes.interfaceMega(earlyShapes, n));
        measure(workloads, "lateInterfaceMono", compiled, n -> HookBenchTypes.lateInterfaceMono(lateShape, n));
        measure(workloads, "lateInterfaceMega", compiled, n -> HookBenchTypes.lateInterfaceMega(lateShapes, n));
        MethodHandle interfaceHandle = MethodHandles.publicLookup().findVirtual(HookBenchTypes.Shape.class,
                "area", MethodType.methodType(int.class, int.class));
        MethodHandle boundHandle = interfaceHandle.bindTo(shape);
        int handleExpected = shape.area(3);
        measure(workloads, "methodHandleExact", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; ++i) total += (int) interfaceHandle.invokeExact(shape, 3);
            return total;
        });
        measure(workloads, "methodHandleBound", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; ++i) total += (int) boundHandle.invokeExact(3);
            return total;
        });
        for (String metric : new String[]{"methodHandleExact", "methodHandleBound"}) {
            JSONObject measured = workloads.getJSONObject(metric);
            long expected = (long) handleExpected * measured.getInt("callsPerBatch") * SAMPLES;
            if (measured.getLong("checksum") != expected) throw new AssertionError("Invalid MethodHandle benchmark checksum");
        }
        long unwind = unwindFromInterpretedCaller();
        int unwindFrames = (int) (unwind >>> 32);
        int unwindEngineFrames = (int) ((unwind >>> 16) & 0xffff);
        int unwindAfterEngine = (int) (unwind & 0xffff);
        setup.put("unwindFrames", unwindFrames).put("unwindEngineFrames", unwindEngineFrames)
                .put("unwindAfterEngine", unwindAfterEngine);
        if (mode.equals("interp") && boundHandleEntryMode() == 2
                && (unwindEngineFrames == 0 || unwindAfterEngine < 2))
            throw new AssertionError("Interpreter wrapper truncated native unwinding");
        setup.put("methodHandleChecks", true);
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
        measure(workloads, "jniIsAssignableFromArray", compiled, n -> jniLoop(target, 3, n));
        JSONObject arrayCheck = workloads.getJSONObject("jniIsAssignableFromArray");
        if (arrayCheck.getLong("checksum") != (long) arrayCheck.getInt("callsPerBatch") * SAMPLES)
            throw new AssertionError("Invalid JNI array covariance checksum");
        setup.put("arrayAssignabilityChecks", true);
        measure(workloads, "classForNameLoaded", compiled, n -> {
            long total = 0;
            for (int i = 0; i < n; i++) total += Class.forName(forName, false, loader).hashCode() & 1;
            return total;
        });
        if (!probe.equals("none")) report.getJSONObject("instrumentationProbe")
                .put("afterWorkloads", JitInstrumentationProbe.state());
        if (engine) report.put("afterWorkloadCapabilities", DcevmRuntime.capabilities());
    }

    /**
     * The default uses 5 ms batches after real-entry warm-up. Explicit
     * conditioning selects 50 ms JIT batches and 2 s of actual-batch warm-up.
     * Selected clock units also warm actual batches for 2 s, with independent
     * references before and after all 21 batches. JNI references surround each
     * allocation-free batch. Every measured batch is retained.
     */
    private void measure(JSONObject into, String name, boolean compiled, Workload workload) throws Throwable {
        int foregroundEpoch = checkForeground();
        boolean unitClock = Arrays.asList(clockUnits.split(",")).contains(name);
        // Android may replace a process task profile between units. Establish
        // affinity at point of use; never repair it during a measured unit.
        if (benchmarkCpu >= 0 && pinCpu(benchmarkCpu) != benchmarkCpu)
            throw new AssertionError("Cannot bind benchmark CPU before " + name);
        int count = 64;
        while (true) {
            long begin = System.nanoTime();
            workload.run(count);
            if (System.nanoTime() - begin >= 5_000_000L || count >= (1 << 26)) break;
            count *= 2;
        }
        if (compiled) {
            // Preserve all eight receiver types in the megamorphic profiles.
            int warmCount = name.endsWith("Mega") ? 8 : 1;
            warm(workload, warmCount, 400_000_000L);
            Thread.sleep(100);
            warm(workload, warmCount, 200_000_000L);
        } else {
            for (int i = 0; i < 3; i++) workload.run(count);
        }
        // Entry warm-up prevents caller inlining from leaving measured entries
        // cold. Actual-batch conditioning separately settles the execution load.
        if (compiled) {
            while (count < (1 << 26)) {
                long begin = System.nanoTime();
                workload.run(count);
                if (System.nanoTime() - begin >= (conditioned ? 50_000_000L : 5_000_000L)) break;
                count *= 2;
            }
        }
        if (unitClock || (conditioned && (compiled || name.equals("jniCallIntMethod")))) {
            long until = System.nanoTime() + 2_000_000_000L;
            do { workload.run(count); } while (System.nanoTime() < until);
        }
        if (benchmarkCpu >= 0 && pinCpu(benchmarkCpu) != benchmarkCpu)
            throw new AssertionError("Cannot bind benchmark CPU after conditioning " + name);
        long[] unitRefsBefore = new long[unitClock ? SAMPLES : 0];
        long[] unitRefsAfter = new long[unitClock ? SAMPLES : 0];
        if (unitClock) for (int i = 0; i < SAMPLES; i++) unitRefsBefore[i] = referenceWork(benchmarkCpu);
        String unitFrequency = unitClock ? frequencyResidency() : "";
        String unitGc = unitClock ? Debug.getRuntimeStat("art.gc.gc-count") : "";
        long[] unitCpu = new long[unitClock ? SAMPLES : 0];
        boolean qualify = conditioned && name.equals("jniCallIntMethod");
        long[] referenceBefore = qualify ? new long[SAMPLES] : null;
        long[] referenceAfter = qualify ? new long[SAMPLES] : null;
        double[] cpuSamples = qualify ? new double[SAMPLES] : null;
        String frequencyBefore = qualify ? frequencyResidency() : "";
        String gcBefore = qualify ? Debug.getRuntimeStat("art.gc.gc-count") : "";
        double[] samples = new double[SAMPLES];
        long checksum = 0;
        for (int i = 0; i < SAMPLES; i++) {
            if (qualify) referenceBefore[i] = referenceWork(benchmarkCpu);
            long unitBegin = unitClock ? Debug.threadCpuTimeNanos() : 0;
            long cpuBegin = qualify ? Debug.threadCpuTimeNanos() : 0;
            long begin = System.nanoTime();
            checksum += workload.run(count);
            samples[i] = (System.nanoTime() - begin) / (double) count;
            if (unitClock) unitCpu[i] = Debug.threadCpuTimeNanos() - unitBegin;
            if (qualify) {
                cpuSamples[i] = (Debug.threadCpuTimeNanos() - cpuBegin) / (double) count;
                referenceAfter[i] = referenceWork(benchmarkCpu);
            }
        }
        String unitFrequencyAfter = unitClock ? frequencyResidency() : "";
        String unitGcAfter = unitClock ? Debug.getRuntimeStat("art.gc.gc-count") : "";
        if (unitClock) for (int i = 0; i < SAMPLES; i++) unitRefsAfter[i] = referenceWork(benchmarkCpu);
        if (qualify) {
            String gcAfter = Debug.getRuntimeStat("art.gc.gc-count");
            String frequencyAfter = frequencyResidency();
            report.put("steadyConditions", new JSONObject()
                    .put("cpuSamplesNs", new JSONArray(cpuSamples))
                    .put("referenceBeforeNs", new JSONArray(referenceBefore))
                    .put("referenceAfterNs", new JSONArray(referenceAfter))
                    .put("frequencyBefore", frequencyBefore).put("frequencyAfter", frequencyAfter)
                    .put("gcBefore", gcBefore).put("gcAfter", gcAfter));
        }
        double[] sorted = samples.clone();
        Arrays.sort(sorted);
        JSONArray raw = new JSONArray();
        for (double sample : samples) raw.put(Math.round(sample * 1000) / 1000.0);
        if (benchmarkCpu >= 0 && pinnedCpu() != benchmarkCpu)
            throw new AssertionError("Benchmark CPU binding lost after " + name);
        JSONObject measured = new JSONObject().put("callsPerBatch", count)
                .put("cpuAffinity", benchmarkCpu)
                .put("medianNs", sorted[SAMPLES / 2]).put("minNs", sorted[0])
                .put("p10Ns", sorted[SAMPLES / 10]).put("p90Ns", sorted[SAMPLES - 1 - SAMPLES / 10])
                .put("samplesNs", raw).put("checksum", checksum);
        if (foregroundActivity != null) {
            if (checkForeground() != foregroundEpoch)
                throw new AssertionError("Foreground benchmark was interrupted during " + name);
            measured.put("foregroundEpoch", foregroundEpoch).put("foregroundCpuset", "/top-app");
        }
        if (unitClock) measured.put("clockConditions", new JSONObject()
                .put("referenceBeforeNs", new JSONArray(unitRefsBefore))
                .put("referenceAfterNs", new JSONArray(unitRefsAfter))
                .put("cpuSamplesNs", new JSONArray(unitCpu))
                .put("frequencyBefore", unitFrequency).put("frequencyAfter", unitFrequencyAfter)
                .put("gcBefore", unitGc).put("gcAfter", unitGcAfter));
        into.put(name, measured);
        writeReport();
    }

    private String frequencyResidency() {
        if (benchmarkCpu < 0) return "";
        String path = "/sys/devices/system/cpu/cpu" + benchmarkCpu + "/cpufreq/stats/time_in_state";
        try (InputStream input = new java.io.FileInputStream(path);
             ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            for (int n; (n = input.read(buffer)) != -1;) bytes.write(buffer, 0, n);
            return bytes.toString("UTF-8");
        } catch (IOException unavailable) {
            return "";
        }
    }

    private static void warm(Workload workload, int count, long nanos) throws Throwable {
        // A Java warm loop may inline its target while leaving the separate
        // measured entry cold. Cross JNI for each actual workload invocation.
        // The native loop also guarantees a full 16-bit counter's worth of
        // real entries before each warm phase completes.
        warmNative(workload, count, nanos);
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
