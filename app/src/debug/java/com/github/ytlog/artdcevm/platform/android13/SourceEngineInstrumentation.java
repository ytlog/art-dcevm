package com.github.ytlog.artdcevm.platform.android13;

import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Debug;
import android.os.Process;
import android.util.Log;
import com.github.ytlog.artdcevm.testing.TestRuntimeBackend;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.testing.TestRuntimeBackend.PatchRequest;
import com.github.ytlog.artdcevm.testing.TestRuntimeBackend.PatchResult;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.Child;
import com.github.ytlog.artdcevm.fixture.DeletionControl;
import com.github.ytlog.artdcevm.fixture.Peer;
import com.github.ytlog.artdcevm.fixture.FieldSubject;
import com.github.ytlog.artdcevm.fixture.FieldChild;
import com.github.ytlog.artdcevm.fixture.SpecialSubject;
import com.github.ytlog.artdcevm.fixture.NativeShape;
import com.github.ytlog.artdcevm.fixture.HierarchySubject;
import com.github.ytlog.artdcevm.fixture.HierarchyChild;
import com.github.ytlog.artdcevm.fixture.HierarchyBase;
import com.github.ytlog.artdcevm.fixture.HierarchyRoot;
import com.github.ytlog.artdcevm.fixture.HierarchyInserted;
import com.github.ytlog.artdcevm.fixture.HierarchyOuter;
import com.github.ytlog.artdcevm.fixture.HierarchyTag;
import com.github.ytlog.artdcevm.fixture.HierarchyMore;
import com.github.ytlog.artdcevm.fixture.NarrowParent;
import com.github.ytlog.artdcevm.fixture.NarrowAlternate;
import com.github.ytlog.artdcevm.fixture.NarrowSubject;
import com.github.ytlog.artdcevm.testing.StandardJvmtiClient;
import com.github.ytlog.artdcevm.fixture.NarrowChild;
import com.github.ytlog.artdcevm.fixture.NarrowHotCaller;
import com.github.ytlog.artdcevm.fixture.NarrowParentHotCaller;
import com.github.ytlog.artdcevm.fixture.NarrowNewParentCaller;
import com.github.ytlog.artdcevm.fixture.NarrowSuperHotCaller;
import com.github.ytlog.artdcevm.fixture.NarrowTypeHotCaller;
import com.github.ytlog.artdcevm.fixture.NarrowBoundHandleCaller;
import com.github.ytlog.artdcevm.fixture.InterfaceTag;
import com.github.ytlog.artdcevm.fixture.InterfaceSubject;
import com.github.ytlog.artdcevm.fixture.InterfacePeer;
import com.github.ytlog.artdcevm.fixture.InterfaceSubTag;
import com.github.ytlog.artdcevm.fixture.InterfaceSubSubject;
import com.github.ytlog.artdcevm.fixture.InterfaceChild;
import com.github.ytlog.artdcevm.fixture.InterfaceOldCaller;
import com.github.ytlog.artdcevm.fixture.StorageSubject;
import com.github.ytlog.artdcevm.fixture.StorageChild;
import com.github.ytlog.artdcevm.fixture.ModifierSubject;
import com.github.ytlog.artdcevm.fixture.ModifierPassive;
import com.github.ytlog.artdcevm.fixture.ModifierHotCaller;
import com.github.ytlog.artdcevm.fixture.FinalFieldSubject;
import com.github.ytlog.artdcevm.fixture.ClassFinalSubject;
import com.github.ytlog.artdcevm.fixture.ClassFinalHotCaller;
import com.github.ytlog.artdcevm.fixture.AbstractMethodSubject;
import com.github.ytlog.artdcevm.fixture.AbstractMethodOverride;
import com.github.ytlog.artdcevm.fixture.AbstractMethodHotCaller;
import com.github.ytlog.artdcevm.fixture.ClassAbstractSubject;
import com.github.ytlog.artdcevm.fixture.ClassAbstractChild;
import com.github.ytlog.artdcevm.fixture.ClassAbstractHotCaller;
import com.github.ytlog.artdcevm.fixture.ClassAbstractNoInstance;
import com.github.ytlog.artdcevm.fixture.CombinedAbstractSubject;
import com.github.ytlog.artdcevm.fixture.CombinedAbstractChild;
import com.github.ytlog.artdcevm.fixture.CombinedAbstractHotCaller;
import com.github.ytlog.artdcevm.fixture.PrivateMethodSubject;
import com.github.ytlog.artdcevm.fixture.PrivateStaticMethodSubject;
import com.github.ytlog.artdcevm.fixture.PrivateMethodHotCaller;
import com.github.ytlog.artdcevm.fixture.PrivateFieldSubject;
import com.github.ytlog.artdcevm.fixture.PrivateFieldHotCaller;
import com.github.ytlog.artdcevm.external.PrivateMethodExternalChild;
import com.github.ytlog.artdcevm.fixture.InlineCallee;
import com.github.ytlog.artdcevm.fixture.InlineHotCaller;
import com.github.ytlog.artdcevm.fixture.InlineActiveCallee;
import com.github.ytlog.artdcevm.fixture.InlineActiveCaller;
import com.github.ytlog.artdcevm.fixture.BootInlineConsumer;
import com.github.ytlog.artdcevm.testing.FinalFieldCaller;
import com.github.ytlog.artdcevm.testing.FinalFieldHandleCaller;
import com.github.ytlog.artdcevm.testing.StorageProbe;
import com.github.ytlog.artdcevm.testing.NativeShapeProbe;
import com.github.ytlog.artdcevm.testing.StaticVisibilityProbe;
import com.github.ytlog.artdcevm.testing.HierarchyProbe;
import java.lang.reflect.Constructor;
import com.github.ytlog.artdcevm.testing.NativeTestBridge;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.ByteBuffer;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.reflect.InvocationTargetException;
import java.lang.ref.WeakReference;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.ArrayList;
import java.util.function.Consumer;
import dalvik.system.InMemoryDexClassLoader;
import dalvik.system.DexClassLoader;
import org.json.JSONArray;
import org.json.JSONObject;

/** Separate process run for source ABI and member evolution conformance. */
@android.annotation.TargetApi(33)
public final class SourceEngineInstrumentation extends Instrumentation {
    private TestRuntimeBackend backend;
    private boolean standardClientAttached;
    private String hookOrder;
    private String suite;
    private int redefine(Class<?> target, byte[] dex) {
        return redefineBatch(new Class<?>[]{target}, new byte[][]{dex});
    }
    private int redefineBatch(Class<?>[] targets, byte[][] dexes) {
        return standardClientAttached ? StandardJvmtiClient.redefine(targets, dexes)
                : backend.apply(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA, targets, dexes)).code;
    }
    private static native int commitCalls();
    private static native int inPlaceInvalidationCalls();
    private static native long readBarrierCalls();
    private static native long classLoaderDeleteCalls();
    private static native long retiredPrunedCount(int kind);
    private static native long retiredFieldSlotCount();
    private static native int jitMethodState(Method method);
    private static native int aotMethodState(Method method);
    private static native int aotExperiment(Method method, boolean enable);
    private static native int aotFrameState(Thread thread, Method method);
    private static native int aotSuspendDeoptProbe(Thread thread, Method method);
    private static native long aotSuspendDeoptHits(boolean reset);
    private static native int jitActiveFrameState(Thread thread, Method caller, Method callee);
    private static native int jitInlineState(Method method);
    private static native int jitInlineExperiment(boolean enable);
    private static native int jitCodeKind(Method method);
    private static native int jitCacheDetail(Method method);
    private static native int invalidateAllJit();
    private static native int forceOptimizedJit(Method method);
    private static native int jitCompilerOptionsState();
    private static native int configureGraphInlineProbe(Method caller, Method callee, boolean enable);
    private static native long graphInlineHits();
    private static native boolean allMethodsDeoptimized();
    private static native boolean fieldReadProbeMode(boolean enable, boolean forceReject);
    private static native long fieldReadProbeHits();
    private static native long invokeGuardDiagnostics(int index);
    private static native long receiverGuardTransactionDiagnostics(int index);
    private static native long compiledPrecommitDiagnostics(int index);
    private static native long hierarchyDiagnostics(int index);
    private static native int rejectedCalls();
    private static native int sourceCheckedHandleCalls();
    private static native boolean probeCheckedHandleHook();
    private static native int checkJniMode(int requested);
    private static native int runnableJniProbe(Object carrier,Object fixture);
    private static volatile int sink;
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        hookOrder = args == null ? "validation-first" : args.getString("hookOrder", "validation-first");
        suite = args == null ? "all" : args.getString("suite", "all");
        start();
    }
    @Override public void onStart() {
        System.loadLibrary("artdcevm_test_support");
        JSONObject report = new JSONObject();
        boolean passed = false;
        try {
            report.put("uid", Process.myUid()).put("completed", false)
                    .put("fullDcevmComplete", false);
            save(report);
            backend = new TestRuntimeBackend(getTargetContext());
            Evolving fixture = new Evolving(37);
            report.put("executionPath", "independent Agent JVMTI RedefineClasses")
                    .put("hookOrder", hookOrder);
            boolean beforeInit = redefine(Evolving.class, patch("body")) == 98 && fixture.compute() == 38;
            report.put("uninitializedEvolutionRejected", beforeInit); save(report);
            if (!beforeInit) throw new AssertionError("Uninitialized backend mutated class");
            if (hookOrder.equals("validation-first") && !backend.enableValidationHooks().installed())
                throw new AssertionError("Validation initialization failed");
            DcevmRuntime.initialize(getTargetContext());
            PatchResult initialized = backend.enableEvolution();
            boolean initializedOk = initialized.installed() && backend.capabilities().getBoolean("evolutionAvailable");
            report.put("probeResult", initialized.toString()).put("readOnlyProbePassed", initializedOk)
                    .put("capabilities", backend.capabilities()); save(report);
            if (!initializedOk) throw new AssertionError("Source initialization: " + backend.capabilities());
            StandardJvmtiClient.attach(getTargetContext());
            standardClientAttached = true;
            DcevmRuntime.initialize(getTargetContext());
            boolean repeat = backend.enableEvolution().installed() && backend.enableValidationHooks().installed();
            report.put("repeatedInitializationIdempotent", repeat); save(report);
            if (!repeat) throw new AssertionError("Repeated initialization");
            report.put("suite", suite);
            passed = suite.equals("hierarchy") ? testHierarchyAdditions(report) && testSimpleHierarchyNarrowing(report)
                    : suite.equals("narrow") ? testSimpleHierarchyNarrowing(report)
                    : suite.equals("narrow-empty") ? testEmptyHierarchyNarrowing(report)
                    : suite.equals("narrow-commit") ? testProductionNarrowing(report)
                    : suite.equals("narrow-roundtrip") ? testProductionNarrowRoundTrip(report)
                    : suite.equals("narrow-concurrent") ? testProductionNarrowConcurrent(report)
                    : suite.equals("narrow-special") ? testProductionNarrowSpecial(report)
                    : suite.equals("narrow-swap") ? testProductionNarrowSwap(report)
                    : suite.equals("narrow-swap-batch") ? testProductionNarrowSwapBatch(report, false)
                    : suite.equals("narrow-swap-batch-reverse") ? testProductionNarrowSwapBatch(report, true)
                    : suite.equals("narrow-swap-concurrent") ? testProductionNarrowSwapConcurrent(report, false)
                    : suite.equals("narrow-swap-concurrent-reverse") ? testProductionNarrowSwapConcurrent(report, true)
                    : suite.equals("interface-remove") ? testInterfaceRemoval(report)
                    : suite.equals("interface-roundtrip") ? testInterfaceRoundTrip(report)
                    : suite.equals("interface-concurrent") ? testInterfaceConcurrent(report)
                    : suite.equals("interface-default") ? testInterfaceDefaultMethod(report, false)
                    : suite.equals("interface-default-checkjni") ? testInterfaceDefaultMethod(report, true)
                    : suite.equals("interface-default-shadow") ? testInterfaceDefaultShadow(report, true)
                    : suite.equals("interface-default-special") ? testInterfaceDefaultSpecial(report)
                    : suite.equals("interface-batch") ? testInterfaceBatch(report, false)
                    : suite.equals("interface-three-batch") ? testInterfaceBatch(report, true)
                    : suite.equals("interface-transfer") ? testInterfaceTransfer(report)
                    : suite.equals("interface-mixed-batch") ? testInterfaceMixedBatch(report)
                    : suite.equals("loader-baseline") ? testLoaderUnloadDiagnostic(report, false, false)
                    : suite.equals("loader-evolved") ? testLoaderUnloadDiagnostic(report, true, false)
                    : suite.equals("loader-retired") ? testLoaderUnloadDiagnostic(report, true, true)
                    : suite.equals("loader-field-cycles") ? testLoaderFieldCycles(report, false)
                    : suite.equals("loader-field-concurrent") ? testLoaderFieldCycles(report, true)
                    : suite.equals("loader-mixed") ? testLoaderMixedRetirement(report, false, 8)
                    : suite.equals("loader-mixed-concurrent") ? testLoaderMixedRetirement(report, true, 64)
                    : suite.equals("loader-mixed-long") ? testLoaderMixedRetirement(report, true, 256)
                    : suite.equals("loader-mixed-cache") ? testLoaderMixedCachedReferences(report)
                    : suite.equals("aot-loader") ? testAotLoaderEvolution(report)
                    : suite.equals("aot-experiment") ? testAotExperiment(report)
                    : suite.equals("aot-active-evolve") ? testAotActiveEvolution(report)
                    : suite.equals("aot-active-frame") ? testAotActiveFrame(report)
                    : suite.equals("aot-compiled-caller") ? testAotCompiledCaller(report)
                    : suite.equals("aot-interface-remove") ? testAotInterfaceRemoval(report, false)
                    : suite.equals("aot-interface-default") ? testAotInterfaceRemoval(report, true)
                    : suite.equals("aot-parent-swap") ? testAotParentSwap(report, false, false)
                    : suite.equals("aot-parent-swap-two") ? testAotParentSwap(report, true, false)
                    : suite.equals("aot-parent-swap-nondebug") ? testAotParentSwap(report, false, true)
                    : suite.equals("runnable-jni") ? testRunnableJniBridge(report)
                    : suite.equals("aot-suspend-deopt") ? testAotSuspendDeopt(report)
                    : suite.equals("jit-parent-swap-spin") ? testJitParentSwapSpin(report)
                    : suite.equals("jit-inline") ? testJitInlineEvolution(report)
                    : suite.equals("jit-inline-optimized") ? testJitOptimizedInlineEvolution(report)
                    : suite.equals("jit-inline-active") ? testJitActiveInlineFrame(report)
                    : suite.equals("jit-boot-inline-diagnostic") ? testBootJitInlineDiagnostic(report)
                    : suite.equals("field-read") ? testArtFieldReadProbe(report)
                    : suite.equals("jni-receiver") ? testJniReceiverProbe(report)
                    : suite.equals("private") ? testPrivateMethodTransition(report) && testPrivateFieldTransition(report)
                    : suite.equals("private-visibility") ? testPrivateVisibilityVariants(report)
                        && testPrivateStaticVisibility(report)
                    : suite.equals("volatile") ? testVolatileFieldTransition(report)
                    : suite.equals("modifier") ? testVisibilityWidening(report) && testFinalFieldTransition(report)
                        && testVolatileFieldTransition(report)
                        && testClassFinalTransition(report) && testAbstractMethodTransition(report)
                        && testCombinedAbstractTransition(report) && testClassAbstractTransition(report)
                        && testPrivateMethodTransition(report) && testPrivateFieldTransition(report)
                        && testPrivateVisibilityVariants(report) && testPrivateStaticVisibility(report)
                    : testRedefinition(report, fixture) && testPublicLifecycle(report) && testSpecialMembers(report)
                    && testNativeShapes(report) && testStorageEvolution(report) && testHierarchyAdditions(report)
                    && testVisibilityWidening(report) && testFinalFieldTransition(report)
                    && testVolatileFieldTransition(report)
                    && testClassFinalTransition(report) && testAbstractMethodTransition(report)
                    && testCombinedAbstractTransition(report) && testClassAbstractTransition(report)
                    && testPrivateMethodTransition(report) && testPrivateFieldTransition(report)
                    && testPrivateVisibilityVariants(report) && testPrivateStaticVisibility(report)
                    && testSimpleHierarchyNarrowing(report) && testInterfaceDefaultShadow(report, false);
            boolean checkedHandleProbe = probeCheckedHandleHook();
            report.put("checkedHandleHookProbe", checkedHandleProbe);
            passed = checkedHandleProbe && passed;
            report.put("capabilitiesAfterEvolution", backend.capabilities()).put("runtimeReadBarrierCalls", readBarrierCalls()).put("sourceCheckedHandleCalls", sourceCheckedHandleCalls());

        } catch (Throwable error) {
            passed = false;
            try { report.put("fatal", Log.getStackTraceString(error)); } catch (Exception ignored) { }
        }
        try { NativeTestBridge.releaseFixture(); NativeTestBridge.releaseSpecialMembers(); NativeShapeProbe.release(); StorageProbe.release(); HierarchyProbe.releaseInterfaceMethod(); HierarchyProbe.release(); } catch (Throwable ignored) { }
        try { report.put("completed", true).put("passed", passed); save(report); } catch (Exception ignored) { }
        Bundle result = new Bundle();
        result.putString("stream", report.toString());
        finish(passed ? -1 : 0, result);
    }
    private boolean testRunnableJniBridge(JSONObject report) throws Exception {
        report.put("hierarchyChecks",new JSONArray()).put("phase","runnable_jni_bridge"); save(report);
        Method carrier=Object.class.getMethod("toString");
        Object fixture=new Object();
        int previous=checkJniMode(-1);
        try {
            for(int mode=0;mode<2;mode++) {
                checkJniMode(mode);
                if(checkJniMode(-1)!=mode)throw new AssertionError("JNI mode unavailable");
                int result=runnableJniProbe(carrier,fixture);
                report.put("runnableJniMode"+mode,result);save(report);
                hierarchyCheck(report,"runnable_jni_real_scope_mode_"+mode,result==1023);
                if(result!=1023)return false;
                int[] workers={-1,-1};
                Throwable[] errors=new Throwable[2];
                final int workerMode=mode;
                Thread[] threads=new Thread[2];
                for(int i=0;i<2;i++) {
                    final int slot=i;
                    threads[i]=new Thread(() -> {
                        int saved=checkJniMode(-1);
                        try {
                            checkJniMode(workerMode);
                            workers[slot]=runnableJniProbe(carrier,fixture);
                        } catch(Throwable error) {errors[slot]=error;}
                        finally {checkJniMode(saved);}
                    },"RunnableJniProbe"+i);
                    threads[i].start();
                }
                for(Thread thread:threads)thread.join(30000);
                report.put("runnableJniWorkersMode"+mode,new JSONArray(workers));save(report);
                hierarchyCheck(report,"runnable_jni_thread_isolation_mode_"+mode,
                    !threads[0].isAlive()&&!threads[1].isAlive()&&errors[0]==null&&errors[1]==null&&
                    workers[0]==1023&&workers[1]==1023);
            }
        } finally {checkJniMode(previous);}
        return true;
    }
    private boolean testRedefinition(JSONObject report, Evolving fixture) throws Exception {
        Evolving alias = fixture;
        Class<?> originalClass = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        Child child = new Child(10);
        Field field = originalClass.getField("value");
        Method method = originalClass.getMethod("compute");
        NativeTestBridge.retainFixture(fixture);
        Evolving.counter = 123;
        report.put("phase", "source_body_commit"); save(report);
        int body = redefine(Evolving.class, patch("body"));
        report.put("bodyResult", body); save(report);
        if (body != 0 || fixture.compute() != 39) return false;
        report.put("phase", "source_additive_commit"); save(report);
        int additive = redefine(Evolving.class, patch("add"));
        int callbacks = commitCalls();
        report.put("additiveResult", additive).put("ownedStructuralCommitCalls", callbacks); save(report);
        if (additive != 0 || callbacks == 0) return false;
        int[] retained = NativeTestBridge.inspectRetainedFixture();
        boolean state = fixture.compute() == 40 && child.compute() == 13 && child.child == 90
                && fixture.value == 37 && Evolving.counter == 123
                && alias == fixture && fixture.getClass() == originalClass
                && System.identityHashCode(fixture) == hash
                && field.getInt(fixture) == 37 && (Integer) method.invoke(fixture) == 40
                && retained[0] == 40 && retained[1] == 37
                && originalClass.getField("added").getLong(fixture) == 0
                && originalClass.getField("added").getLong(new Evolving(1)) == 77;
        report.put("stateAndReferencesPreserved", state); save(report);
        System.gc();
        boolean afterGc = fixture.compute() == 40 && child.compute() == 13;
        report.put("stateAfterGc", afterGc).put("phase", "finished");
        return state && afterGc && testMethodDeletion(report, fixture, child) && testFieldEvolution(report);
    }
    @FunctionalInterface private interface CheckedCall { Object run() throws Throwable; }
    private void expectRemoved(JSONObject report, String name, CheckedCall call) throws Exception {
        report.put("phase", name); save(report);
        Throwable caught = null;
        try { call.run(); } catch (Throwable error) {
            caught = error;
            while (caught instanceof InvocationTargetException && caught.getCause() != null)
                caught = caught.getCause();
        }
        boolean passed = caught instanceof NoSuchMethodError;
        report.getJSONArray("deletionChecks").put(new JSONObject().put("name", name)
                .put("passed", passed).put("exception", caught == null ? "none" : caught.getClass().getName()));
        save(report);
        if (!passed) throw new AssertionError(name + ": expected NoSuchMethodError", caught);
    }
    private boolean testMethodDeletion(JSONObject report, Evolving fixture, Child child) throws Exception {
        report.put("deletionChecks", new JSONArray()).put("phase", "prepare_method_deletion"); save(report);
        int blocking = redefine(Evolving.class, patch("method-blocking"));
        if (blocking != 0) throw new AssertionError("Blocking body install: " + blocking);
        Method cached = Evolving.class.getMethod("addedMethod");
        Method surviving = Evolving.class.getMethod("compute");
        MethodHandle survivingHandle = MethodHandles.lookup().findVirtual(Evolving.class, "compute", MethodType.methodType(int.class));
        MethodHandle exact = MethodHandles.lookup().findVirtual(Evolving.class, "addedMethod", MethodType.methodType(int.class));
        MethodHandle adapted = exact.asType(MethodType.methodType(Object.class, Object.class));
        NativeTestBridge.retainRemovalMethod(fixture);
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset("method-blocking/com/github/ytlog/artdcevm/fixture/RemovalCaller.dex")), Evolving.class.getClassLoader());
        Class<?> caller = loader.loadClass("com.github.ytlog.artdcevm.fixture.RemovalCaller");
        Method hot = caller.getMethod("hot", Evolving.class);
        Method cold = caller.getMethod("cold", Evolving.class);
        for (int i = 0; i < 30000; ++i) sink = (Integer) hot.invoke(null, fixture);
        for (int mode = 0; mode < 4; ++mode)
            if (NativeTestBridge.invokeRemovalMethod(mode) != 237) throw new AssertionError("JNI baseline " + mode);
        DeletionControl.entered = new CountDownLatch(1);
        DeletionControl.release = new CountDownLatch(1);
        AtomicInteger activeResult = new AtomicInteger(-1);
        AtomicReference<Throwable> activeError = new AtomicReference<>();
        Thread active = new Thread(() -> {
            try { activeResult.set((Integer) cached.invoke(fixture)); }
            catch (Throwable error) { activeError.set(error); }
        }, "deleted-method-frame");
        active.start();
        int removed;
        try {
            if (!DeletionControl.entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Old method did not enter");
            report.put("phase", "source_method_delete"); save(report);
            removed = redefine(Evolving.class, patch("method-delete"));
            report.put("methodDeleteResult", removed); save(report);
        } finally {
            DeletionControl.release.countDown();
            active.join(10000);
            DeletionControl.release = null;
        }
        if (removed != 0) throw new AssertionError("Method deletion returned " + removed);
        boolean activePassed = !active.isAlive() && activeResult.get() == 237 && activeError.get() == null;
        report.put("deletedActiveFrameFinishedOldBody", activePassed); save(report);
        if (!activePassed) throw new AssertionError("Deleted active frame", activeError.get());
        boolean absent = false;
        try { Evolving.class.getDeclaredMethod("addedMethod"); } catch (NoSuchMethodException expected) { absent = true; }
        for (Method declared : Evolving.class.getDeclaredMethods())
            if (declared.getName().equals("addedMethod")) absent = false;
        for (Method visible : Evolving.class.getMethods())
            if (visible.getName().equals("addedMethod")) absent = false;
        report.put("removedMethodAbsentFromLookup", absent); save(report);
        if (!absent) throw new AssertionError("Deleted method still declared");
        expectRemoved(report, "cached_reflection", () -> cached.invoke(fixture));
        expectRemoved(report, "warm_direct_call", () -> hot.invoke(null, fixture));
        expectRemoved(report, "cold_direct_call", () -> cold.invoke(null, fixture));
        expectRemoved(report, "cached_method_handle_exact", () -> (int) exact.invokeExact(fixture));
        expectRemoved(report, "cached_method_handle_adapted", () -> adapted.invokeExact((Object) fixture));
        for (int mode = 0; mode < 4; ++mode) {
            final int selected = mode;
            expectRemoved(report, "cached_jni_" + mode, () -> NativeTestBridge.invokeRemovalMethod(selected));
        }
        System.gc();
        expectRemoved(report, "cached_reflection_after_gc", () -> cached.invoke(fixture));
        int[] retained = NativeTestBridge.inspectRetainedFixture();
        boolean state = fixture.compute() == 41 && child.compute() == 14 && child.child == 90
                && retained[0] == 41 && retained[1] == 37 && Evolving.counter == 123;
        report.put("methodDeletionStatePreserved", state).put("rejectedRetiredInvocations", rejectedCalls())
                .put("ownedStructuralCommitCalls", commitCalls()).put("phase", "finished");
        if (!state) return false;
        report.put("phase", "method_reintroduced"); save(report);
        int reintroduced = redefine(Evolving.class, patch("add2"));
        report.put("reintroduceResult", reintroduced); save(report);
        if (reintroduced != 0) throw new AssertionError("Reintroduce failed: " + reintroduced);
        Method fresh = Evolving.class.getMethod("addedMethod");
        if ((Integer) fresh.invoke(fixture) != 137) throw new AssertionError("Fresh method result");
        expectRemoved(report, "old_reflection_after_reintroduce", () -> cached.invoke(fixture));
        expectRemoved(report, "old_handle_after_reintroduce", () -> (int) exact.invokeExact(fixture));
        expectRemoved(report, "old_jni_after_reintroduce", () -> NativeTestBridge.invokeRemovalMethod(0));
        // A valid removal followed by invalid bytes must publish neither class.
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{Evolving.class, Peer.class},
                new byte[][]{patch("method-delete"), new byte[]{1, 2, 3}});
        boolean atomic = invalid != 0 && commitCalls() == before && (Integer) fresh.invoke(fixture) == 137
                && fixture.compute() == 42 && new Peer().compute() == 11;
        report.put("invalidBatchResult", invalid).put("invalidRemovalBatchAtomic", atomic); save(report);
        if (!atomic) throw new AssertionError("Invalid batch changed live state");
        MethodHandle previousSignature = MethodHandles.lookup().findVirtual(Evolving.class, "addedMethod", MethodType.methodType(int.class));
        report.put("phase", "method_signature_commit"); save(report);
        Peer peer = new Peer();
        int signature = redefineBatch(new Class<?>[]{Evolving.class, Peer.class},
                new byte[][]{patch("method-signature"), asset("peer-added/com/github/ytlog/artdcevm/fixture/Peer.dex")});
        report.put("methodSignatureResult", signature); save(report);
        if (signature != 0) throw new AssertionError("Signature change returned " + signature);
        expectRemoved(report, "previous_signature_reflection", () -> fresh.invoke(fixture));
        expectRemoved(report, "previous_signature_handle", () -> (int) previousSignature.invokeExact(fixture));
        Method changed = Evolving.class.getMethod("addedMethod", long.class);
        MethodHandle changedHandle = MethodHandles.lookup().findVirtual(Evolving.class, "addedMethod", MethodType.methodType(long.class, long.class));
        long argument = 10_000_000_000L;
        long expected = argument + 37;
        boolean signaturePassed;
        try {
            signaturePassed = (Long) changed.invoke(fixture, argument) == expected
                    && (long) changedHandle.invokeExact(fixture, argument) == expected
                    && NativeTestBridge.invokeSignatureMethod(fixture, argument) == expected
                    && (int) survivingHandle.invokeExact(fixture) == 42
                    && (Integer) surviving.invoke(fixture) == 42;
        } catch (Throwable error) { throw new AssertionError("New signature invocation", error); }
        report.put("newSignatureReflectionHandleJni", signaturePassed); save(report);
        if (!signaturePassed) return false;
        boolean peerState = peer.compute() == 12 && Peer.class.getField("marker").getLong(peer) == 0;
        report.put("twoClassBatchPublished", peerState); save(report);
        if (!peerState) return false;
        System.gc();
        expectRemoved(report, "first_generation_reflection_after_gc", () -> cached.invoke(fixture));
        expectRemoved(report, "first_generation_jni_after_gc", () -> NativeTestBridge.invokeRemovalMethod(0));
        report.put("ownedStructuralCommitCalls", commitCalls()).put("rejectedRetiredInvocations", rejectedCalls())
                .put("phase", "finished");
        int[] finalJni = NativeTestBridge.inspectRetainedFixture();
        return fixture.compute() == 42 && child.compute() == 15 && Evolving.counter == 123
                && finalJni[0] == 42 && finalJni[1] == 37;
    }
    private void expectFieldRemoved(JSONObject report, String name, CheckedCall call) throws Exception {
        report.put("phase", name); save(report);
        Throwable caught = null;
        try { call.run(); } catch (Throwable error) {
            caught = error;
            while (caught instanceof InvocationTargetException && caught.getCause() != null)
                caught = caught.getCause();
        }
        boolean passed = caught instanceof NoSuchFieldError;
        report.getJSONArray("fieldChecks").put(new JSONObject().put("name", name)
                .put("passed", passed).put("exception", caught == null ? "none" : caught.getClass().getName()));
        save(report);
        if (!passed) throw new AssertionError(name + ": expected NoSuchFieldError", caught);
    }
    private static Throwable cause(Throwable value) {
        while (value instanceof InvocationTargetException && value.getCause() != null) value = value.getCause();
        return value;
    }
    private boolean testFieldEvolution(JSONObject report) throws Exception {
        int initial = checkJniMode(-1);
        if (initial < 0) throw new AssertionError("Unrecognized JNI function table");
        checkJniMode(1);
        try {
            if (checkJniMode(-1) != 1) throw new AssertionError("Checked JNI mode unavailable");
            report.put("fieldCheckJniInitiallyEnabled", initial == 1).put("checkedJniFieldGuards", true);
            return testFieldEvolutionChecked(report);
        } finally { checkJniMode(initial); }
    }
    private boolean testFieldEvolutionChecked(JSONObject report) throws Exception {
        report.put("fieldChecks", new JSONArray()).put("phase", "prepare_field_deletion"); save(report);
        FieldSubject fixture = new FieldSubject();
        FieldSubject alias = fixture;
        Class<?> originalClass = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        fixture.keep = 101;
        FieldSubject.staticKeep = 102;
        FieldChild child = new FieldChild();
        Field kept = FieldSubject.class.getField("keep");
        MethodHandle keptHandle = MethodHandles.lookup().findGetter(FieldSubject.class, "keep", int.class);
        String[] names = {"z", "b", "c", "s", "i", "j", "f", "d", "o",
                "sz", "sb", "sc", "ss", "si", "sj", "sf", "sd", "so"};
        String[] suffixes = {"Boolean", "Byte", "Char", "Short", "Int", "Long", "Float", "Double", ""};
        Class<?>[] types = {boolean.class, byte.class, char.class, short.class, int.class, long.class,
                float.class, double.class, Object.class};
        double[] expected = {1, 3, 65, 5, 7, 9_000_000_000L, 1.25, 2.5, 1};
        Field[] fields = new Field[18];
        Object[] values = new Object[18];
        MethodHandle[] getters = new MethodHandle[18], setters = new MethodHandle[18];
        VarHandle[] vars = new VarHandle[18];
        NativeTestBridge.retainFields(fixture);
        for (int i = 0; i < names.length; ++i) {
            fields[i] = FieldSubject.class.getField(names[i]);
            values[i] = fields[i].get(fixture);
            Class<?> type = types[i % 9];
            getters[i] = i < 9 ? MethodHandles.lookup().findGetter(FieldSubject.class, names[i], type)
                               : MethodHandles.lookup().findStaticGetter(FieldSubject.class, names[i], type);
            setters[i] = i < 9 ? MethodHandles.lookup().findSetter(FieldSubject.class, names[i], type)
                               : MethodHandles.lookup().findStaticSetter(FieldSubject.class, names[i], type);
            vars[i] = i < 9 ? MethodHandles.lookup().findVarHandle(FieldSubject.class, names[i], type)
                            : MethodHandles.lookup().findStaticVarHandle(FieldSubject.class, names[i], type);
            if (NativeTestBridge.accessField(i, false) != expected[i % 9]) throw new AssertionError("JNI field baseline " + names[i]);
        }
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset("field-original/com/github/ytlog/artdcevm/fixture/FieldCaller.dex")), FieldSubject.class.getClassLoader());
        int previousCheckJni = checkJniMode(0);
        try {
            if (checkJniMode(-1) != 0) throw new AssertionError("Unchecked JNI mode unavailable");
            for (int i = 0; i < 18; ++i)
                if (NativeTestBridge.accessField(i, false) != expected[i % 9]) throw new AssertionError("Unchecked JNI baseline " + i);
        } finally { checkJniMode(previousCheckJni); }
        Class<?> caller = loader.loadClass("com.github.ytlog.artdcevm.fixture.FieldCaller");
        Method hot = caller.getMethod("hot", FieldSubject.class), cold = caller.getMethod("cold", FieldSubject.class);
        Method set = caller.getMethod("set", FieldSubject.class), staticGet = caller.getMethod("staticGet"), staticSet = caller.getMethod("staticSet");
        int fieldHotCallerJitState = 0, fieldHotCallerWarmInvocations = 0;
        for (int attempt = 0; attempt < 10 && fieldHotCallerJitState != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink = (int) (long) (Long) hot.invoke(null, fixture);
            fieldHotCallerWarmInvocations += 100000;
            Thread.sleep(100);
            fieldHotCallerJitState = jitMethodState(hot);
        }
        report.put("fieldHotCallerJitStateBefore", fieldHotCallerJitState)
                .put("fieldHotCallerWarmInvocations", fieldHotCallerWarmInvocations); save(report);
        if (fieldHotCallerJitState != 2) throw new AssertionError(
                "Field hot caller did not enter the ART JIT code cache: " + fieldHotCallerJitState);
        if ((Integer) staticGet.invoke(null) != 7) throw new AssertionError("Static caller baseline");
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{FieldSubject.class, Peer.class},
                new byte[][]{asset("field-delete/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"), new byte[]{1, 2, 3}});
        boolean atomic = invalid != 0 && commitCalls() == before && fields[5].getLong(fixture) == 9_000_000_000L;
        report.put("invalidFieldBatchAtomic", atomic); save(report);
        if (!atomic) return false;
        DeletionControl.entered = new CountDownLatch(1);
        DeletionControl.release = new CountDownLatch(1);
        AtomicReference<Throwable> activeError = new AtomicReference<>();
        Method activeCall = caller.getMethod("active", FieldSubject.class);
        Thread active = new Thread(() -> {
            try { activeCall.invoke(null, fixture); }
            catch (Throwable error) { activeError.set(cause(error)); }
        }, "removed-field-active-frame");
        active.start();
        int removed;
        try {
            if (!DeletionControl.entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Field frame did not enter");
            report.put("phase", "source_field_delete"); save(report);
            removed = redefine(FieldSubject.class, asset("field-delete/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"));
            report.put("fieldDeleteResult", removed); save(report);
        } finally {
            DeletionControl.release.countDown(); active.join(10000); DeletionControl.release = null;
        }
        if (removed != 0) throw new AssertionError("Field deletion returned " + removed);
        boolean activePassed = !active.isAlive() && activeError.get() instanceof NoSuchFieldError;
        report.put("deletedFieldActiveFrameFailsOnNextAccess", activePassed); save(report);
        if (!activePassed) throw new AssertionError("Active field access", activeError.get());
        boolean absent = FieldSubject.class.getDeclaredFields().length == 2 && FieldSubject.class.getFields().length == 2;
        for (String name : names) {
            try { FieldSubject.class.getDeclaredField(name); absent = false; }
            catch (NoSuchFieldException expectedError) { }
        }
        report.put("deletedFieldsAbsentFromLookup", absent); save(report);
        if (!absent) return false;
        for (int i = 0; i < names.length; ++i) {
            final int slot = i;
            Field field = fields[i]; Object value = values[i];
            Method typedGet = Field.class.getMethod("get" + suffixes[i % 9], Object.class);
            Method typedSet = Field.class.getMethod("set" + suffixes[i % 9], Object.class, types[i % 9]);
            if (!field.getName().equals(names[i]) || field.getType() != types[i % 9] || field.getDeclaringClass() != FieldSubject.class)
                throw new AssertionError("Cached field metadata " + names[i]);
            expectFieldRemoved(report, names[i] + "_reflection_get", () -> field.get(fixture));
            expectFieldRemoved(report, names[i] + "_reflection_set", () -> { field.set(fixture, value); return null; });
            expectFieldRemoved(report, names[i] + "_typed_get", () -> typedGet.invoke(field, fixture));
            expectFieldRemoved(report, names[i] + "_typed_set", () -> typedSet.invoke(field, fixture, value));
            expectFieldRemoved(report, names[i] + "_jni_get", () -> NativeTestBridge.accessField(slot, false));
            expectFieldRemoved(report, names[i] + "_jni_set", () -> NativeTestBridge.accessField(slot, true));
            expectFieldRemoved(report, names[i] + "_handle_get", () -> slot < 9 ? getters[slot].invokeWithArguments(fixture) : getters[slot].invokeWithArguments());
            expectFieldRemoved(report, names[i] + "_handle_set", () -> slot < 9 ? setters[slot].invokeWithArguments(fixture, value) : setters[slot].invokeWithArguments(value));
            expectFieldRemoved(report, names[i] + "_var_get", () -> slot < 9 ? vars[slot].get(fixture) : vars[slot].get());
            expectFieldRemoved(report, names[i] + "_var_set", () -> { if (slot < 9) vars[slot].set(fixture, value); else vars[slot].set(value); return null; });
            expectFieldRemoved(report, names[i] + "_var_cas", () -> slot < 9 ? vars[slot].compareAndSet(fixture, value, value) : vars[slot].compareAndSet(value, value));
        }
        expectFieldRemoved(report, "field_hot_direct", () -> hot.invoke(null, fixture));
        previousCheckJni = checkJniMode(0);
        try {
            for (int i = 0; i < 18; ++i) {
                final int slot = i;
                expectFieldRemoved(report, names[i] + "_unchecked_jni_get", () -> NativeTestBridge.accessField(slot, false));
                expectFieldRemoved(report, names[i] + "_unchecked_jni_set", () -> NativeTestBridge.accessField(slot, true));
            }
        } finally { checkJniMode(previousCheckJni); }
        report.put("uncheckedJniFieldGuards", true); save(report);
        expectFieldRemoved(report, "field_cold_direct", () -> cold.invoke(null, fixture));
        expectFieldRemoved(report, "field_direct_set", () -> set.invoke(null, fixture));
        expectFieldRemoved(report, "field_static_direct_get", () -> staticGet.invoke(null));
        expectFieldRemoved(report, "field_static_direct_set", () -> staticSet.invoke(null));
        System.gc();
        expectFieldRemoved(report, "field_reflection_after_gc", () -> fields[5].getLong(fixture));
        expectFieldRemoved(report, "field_var_after_gc", () -> vars[5].get(fixture));
        boolean state;
        try {
            state = fixture == alias && fixture.getClass() == originalClass && System.identityHashCode(fixture) == hash
                    && fixture.keep == 101 && FieldSubject.staticKeep == 102 && fixture.compute() == 102
                    && child.keep == 41 && child.child == 8888888888L && child.compute() == 42
                    && kept.getInt(fixture) == 101 && (int) keptHandle.invokeExact(fixture) == 101;
        } catch (Throwable error) { throw new AssertionError("Retained field", error); }
        report.put("fieldDeletionStatePreserved", state); save(report);
        if (!state) return false;
        report.put("phase", "field_reintroduce"); save(report);
        int restored = redefine(FieldSubject.class, asset("field-original/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"));
        report.put("fieldReintroduceResult", restored); save(report);
        if (restored != 0) return false;
        Field fresh = FieldSubject.class.getField("i"), freshStatic = FieldSubject.class.getField("si");
        if (fresh.getInt(fixture) != 0 || freshStatic.getInt(null) != 0 || FieldSubject.class.getField("o").get(fixture) != null)
            throw new AssertionError("Reintroduced fields did not use defaults");
        expectFieldRemoved(report, "first_field_reflection_after_reintroduce", () -> fields[4].getInt(fixture));
        expectFieldRemoved(report, "first_field_jni_after_reintroduce", () -> NativeTestBridge.accessField(4, false));
        expectFieldRemoved(report, "first_field_var_after_reintroduce", () -> vars[4].get(fixture));
        fresh.setInt(fixture, 12345); freshStatic.setInt(null, 54321);
        NativeTestBridge.retainFields(fixture);
        VarHandle freshVar = MethodHandles.lookup().findVarHandle(FieldSubject.class, "i", int.class);
        report.put("phase", "field_type_commit"); save(report);
        int changed = redefine(FieldSubject.class, asset("field-types/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"));
        report.put("fieldTypeResult", changed); save(report);
        if (changed != 0) return false;
        expectFieldRemoved(report, "old_int_reflection_after_type_change", () -> fresh.getInt(fixture));
        expectFieldRemoved(report, "old_static_int_reflection_after_type_change", () -> freshStatic.getInt(null));
        expectFieldRemoved(report, "old_int_jni_after_type_change", () -> NativeTestBridge.accessField(4, false));
        expectFieldRemoved(report, "old_static_int_jni_after_type_change", () -> NativeTestBridge.accessField(13, false));
        expectFieldRemoved(report, "old_int_var_after_type_change", () -> freshVar.get(fixture));
        Field newLong = FieldSubject.class.getField("i"), newObject = FieldSubject.class.getField("si");
        boolean defaults = newLong.getType() == long.class && newLong.getLong(fixture) == 0 && newObject.getType() == Object.class && newObject.get(null) == null;
        if (!defaults) throw new AssertionError("Field type change reinterpreted old bits");
        long large = 10_000_000_001L;
        newLong.setLong(fixture, large);
        VarHandle newVar = MethodHandles.lookup().findVarHandle(FieldSubject.class, "i", long.class);
        if ((long) newVar.get(fixture) != large || NativeTestBridge.accessChangedLongField(fixture, large + 1) != large + 1)
            throw new AssertionError("New long field reflection/VarHandle/JNI");
        Object reference = new Object(); newObject.set(null, reference);
        System.gc();
        boolean changedState = newObject.get(null) == reference && newLong.getLong(fixture) == large + 1
                && fixture.keep == 101 && fixture.compute() == 103 && child.child == 8888888888L && FieldSubject.staticKeep == 102;
        expectFieldRemoved(report, "first_field_after_type_change_gc", () -> fields[4].get(fixture));
        expectFieldRemoved(report, "first_field_var_after_type_change_gc", () -> vars[4].get(fixture));
        report.put("fieldTypeDefaultsAndNewAccess", changedState).put("phase", "field_reference_to_primitive"); save(report);
        if (!changedState) return false;
        int reversed = redefine(FieldSubject.class, asset("field-types2/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"));
        report.put("fieldReferenceToPrimitiveResult", reversed); save(report);
        if (reversed != 0) return false;
        expectFieldRemoved(report, "old_long_after_reference_change", () -> newLong.getLong(fixture));
        expectFieldRemoved(report, "old_reference_after_primitive_change", () -> newObject.get(null));
        expectFieldRemoved(report, "old_long_var_after_reference_change", () -> newVar.get(fixture));
        Field referenceField = FieldSubject.class.getField("i"), primitiveField = FieldSubject.class.getField("si");
        if (referenceField.get(fixture) != null || primitiveField.getLong(null) != 0) throw new AssertionError("Reverse type defaults");
        referenceField.set(fixture, reference); primitiveField.setLong(null, large);
        System.gc();
        boolean reverseState = referenceField.get(fixture) == reference && primitiveField.getLong(null) == large
                && fixture.keep == 101 && fixture.compute() == 104 && child.child == 8888888888L && FieldSubject.staticKeep == 102;
        report.put("referencePrimitiveEvolutionState", reverseState).put("ownedStructuralCommitCalls", commitCalls()).put("phase", "finished");
        return reverseState;
    }
    private boolean testPublicLifecycle(JSONObject report) throws Exception {
        report.put("phase", "public_backend_policy"); save(report);
        int before = commitCalls();
        byte[] same = patch("method-signature");
        PatchResult duplicate = backend.apply(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA,
                new Class<?>[]{Evolving.class, Evolving.class}, new byte[][]{same, same}));
        boolean duplicateRejected = duplicate.code == 103 && commitCalls() == before;
        report.put("duplicatePublicBatchRejected", duplicateRejected); save(report);
        PatchResult stock = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_STRUCTURE,
                new Class<?>[]{FieldSubject.class}, new byte[][]{asset("field-delete/com/github/ytlog/artdcevm/fixture/FieldSubject.dex")}));
        boolean stockPolicy = !stock.installed() && commitCalls() == before && FieldSubject.class.getField("i").getType() == Object.class;
        report.put("stockAdditivePolicyPreserved", stockPolicy); save(report);
        PatchResult hierarchy = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_STRUCTURE,
                new Class<?>[]{Evolving.class}, new byte[][]{patch("hierarchy")}));
        boolean hierarchyRejected = !hierarchy.installed() && commitCalls() == before && Evolving.class.getSuperclass() == Object.class;
        report.put("stockHierarchyRejectedAtomically", hierarchyRejected).put("hierarchyError", hierarchy.toString()); save(report);
        PatchResult body = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_BODY,
                new Class<?>[]{Evolving.class}, new byte[][]{same}));
        boolean bodyPolicy = body.installed() && commitCalls() == before && new Evolving(1).compute() == 6;
        report.put("stockBodyWorksAfterEvolution", bodyPolicy); save(report);
        if (!duplicateRejected || !stockPolicy || !hierarchyRejected || !bodyPolicy) return false;
        TestRuntimeBackend another = new TestRuntimeBackend(getTargetContext());
        if (!another.enableEvolution().installed() || !another.enableValidationHooks().installed())
            throw new AssertionError("Second backend could not reuse initialization");
        PatchRequest peer = new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA, new Class<?>[]{Peer.class},
                new byte[][]{asset("peer-added2/com/github/ytlog/artdcevm/fixture/Peer.dex")});
        PatchRequest fields = new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA, new Class<?>[]{FieldSubject.class},
                new byte[][]{asset("field-types3/com/github/ytlog/artdcevm/fixture/FieldSubject.dex")});
        CountDownLatch gate = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread first = new Thread(() -> {
            try { gate.await(); if (!backend.apply(peer).installed()) throw new AssertionError("Peer worker"); }
            catch (Throwable error) { failure.compareAndSet(null, error); }
        }, "sdk-peer-patch");
        Thread second = new Thread(() -> {
            try { gate.await(); if (!another.apply(fields).installed()) throw new AssertionError("Field worker"); }
            catch (Throwable error) { failure.compareAndSet(null, error); }
        }, "sdk-field-patch");
        report.put("phase", "concurrent_public_backends"); save(report);
        first.start(); second.start(); gate.countDown();
        first.join(10000); second.join(10000);
        if (first.isAlive() || second.isAlive() || failure.get() != null) throw new AssertionError("Concurrent backend applications", failure.get());
        boolean concurrent = new Peer().compute() == 13 && new FieldSubject().compute() == 45 && FieldSubject.staticKeep == 102;
        report.put("concurrentPublicBackendsPassed", concurrent).put("phase", "finished"); save(report);
        return concurrent;
    }
    private void expectSpecialRemoved(JSONObject report, String name, CheckedCall call) throws Exception {
        report.put("phase", name); save(report);
        Throwable caught = null;
        try { call.run(); } catch (Throwable error) { caught = cause(error); }
        boolean ok = caught instanceof NoSuchMethodError;
        report.getJSONArray("specialMemberChecks").put(new JSONObject().put("name", name)
                .put("passed", ok).put("exception", caught == null ? "none" : caught.getClass().getName()));
        save(report);
        if (!ok) throw new AssertionError(name + ": expected NoSuchMethodError", caught);
    }
    private boolean testSpecialMembers(JSONObject report) throws Exception {
        report.put("specialMemberChecks", new JSONArray()).put("phase", "prepare_special_members"); save(report);
        NativeTestBridge.registerSpecialNatives(SpecialSubject.class);
        SpecialSubject fixture = new SpecialSubject();
        SpecialSubject alias = fixture;
        Class<?> identity = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        SpecialSubject.initialized = 717;
        Field staticField = SpecialSubject.class.getField("initialized");
        Constructor<?> constructor = SpecialSubject.class.getConstructor(int.class);
        Constructor<?> defaultConstructor = SpecialSubject.class.getConstructor();
        Method nativeMethod = SpecialSubject.class.getMethod("nativeGone", int.class);
        Method staticMethod = SpecialSubject.class.getMethod("nativeStatic", long.class);
        Method kept = SpecialSubject.class.getMethod("nativeKept");
        MethodHandle ctorHandle = MethodHandles.lookup().findConstructor(SpecialSubject.class, MethodType.methodType(void.class, int.class));
        MethodHandle nativeHandle = MethodHandles.lookup().findVirtual(SpecialSubject.class, "nativeGone", MethodType.methodType(int.class, int.class));
        MethodHandle staticHandle = MethodHandles.lookup().findStatic(SpecialSubject.class, "nativeStatic", MethodType.methodType(long.class, long.class));
        NativeTestBridge.retainSpecialMembers(fixture);
        for (int mode = 0; mode < 4; ++mode)
            if (((SpecialSubject) NativeTestBridge.createSpecial(mode, 3)).value != 13) throw new AssertionError("Constructor JNI baseline " + mode);
        for (int mode = 0; mode < 8; ++mode) {
            long expected = mode < 4 ? 127 : mode < 7 ? 2006 : 323;
            if (NativeTestBridge.invokeSpecialNative(mode) != expected) throw new AssertionError("Native JNI baseline " + mode);
        }
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset("special-original/com/github/ytlog/artdcevm/fixture/SpecialCaller.dex")), SpecialSubject.class.getClassLoader());
        Class<?> caller = loader.loadClass("com.github.ytlog.artdcevm.fixture.SpecialCaller");
        Method ctorHot = caller.getMethod("constructorHot", int.class), ctorCold = caller.getMethod("constructorCold", int.class);
        Method nativeHot = caller.getMethod("nativeHot", SpecialSubject.class), nativeCold = caller.getMethod("nativeCold", SpecialSubject.class);
        Method staticHot = caller.getMethod("staticHot"), staticCold = caller.getMethod("staticCold");
        for (int i = 0; i < 10000; ++i) {
            sink = ((SpecialSubject) ctorHot.invoke(null, 2)).value;
            sink = (Integer) nativeHot.invoke(null, fixture);
            sink = (int) (long) (Long) staticHot.invoke(null);
        }
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{SpecialSubject.class, Peer.class},
                new byte[][]{asset("special-delete/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"), new byte[]{1, 2, 3}});
        boolean atomic = invalid != 0 && commitCalls() == before && (Integer) nativeMethod.invoke(fixture, 4) == 127;
        report.put("invalidSpecialBatchAtomic", atomic); save(report);
        if (!atomic) return false;
        DeletionControl.entered = new CountDownLatch(1); DeletionControl.release = new CountDownLatch(1);
        AtomicReference<SpecialSubject> constructed = new AtomicReference<>();
        AtomicReference<Throwable> ctorError = new AtomicReference<>(), nativeError = new AtomicReference<>();
        AtomicInteger nativeResult = new AtomicInteger();
        Thread ctorThread = new Thread(() -> {
            try { constructed.set((SpecialSubject) constructor.newInstance(7)); }
            catch (Throwable error) { ctorError.set(cause(error)); }
        }, "obsolete-constructor");
        Thread nativeThread = new Thread(() -> {
            try { nativeResult.set((Integer) nativeMethod.invoke(fixture, -1)); }
            catch (Throwable error) { nativeError.set(cause(error)); }
        }, "obsolete-native-frame");
        ctorThread.start(); nativeThread.start();
        int removed;
        try {
            if (!DeletionControl.entered.await(10, TimeUnit.SECONDS) || !NativeTestBridge.waitSpecialNative())
                throw new AssertionError("Special frames did not enter");
            report.put("phase", "special_members_delete_commit"); save(report);
            removed = redefine(SpecialSubject.class, asset("special-delete/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"));
            report.put("specialDeleteResult", removed); save(report);
        } finally {
            DeletionControl.release.countDown(); NativeTestBridge.releaseSpecialNative();
            ctorThread.join(10000); nativeThread.join(10000); DeletionControl.release = null;
        }
        if (removed != 0) throw new AssertionError("Special deletion: " + removed);
        boolean frames = !ctorThread.isAlive() && !nativeThread.isAlive() && ctorError.get() == null && nativeError.get() == null
                && constructed.get() != null && constructed.get().value == 17 && constructed.get().getClass() == SpecialSubject.class
                && nativeResult.get() == 1023;
        report.put("deletedConstructorAndNativeFramesFinish", frames); save(report);
        if (!frames) throw new AssertionError("Active constructor/native frame: " + ctorError.get() + "; " + nativeError.get());
        boolean absent = SpecialSubject.class.getDeclaredConstructors().length == 1;
        for (Method method : SpecialSubject.class.getDeclaredMethods())
            if (method.getName().equals("nativeGone") || method.getName().equals("nativeStatic")) absent = false;
        try { SpecialSubject.class.getConstructor(int.class); absent = false; } catch (NoSuchMethodException expected) { }
        report.put("specialMembersAbsentFromLookup", absent); save(report);
        if (!absent) return false;
        expectSpecialRemoved(report, "constructor_reflection", () -> constructor.newInstance(1));
        expectSpecialRemoved(report, "constructor_handle", () -> ctorHandle.invokeWithArguments(1));
        expectSpecialRemoved(report, "constructor_hot_direct", () -> ctorHot.invoke(null, 1));
        expectSpecialRemoved(report, "constructor_cold_direct", () -> ctorCold.invoke(null, 1));
        expectSpecialRemoved(report, "native_reflection", () -> nativeMethod.invoke(fixture, 4));
        expectSpecialRemoved(report, "static_native_reflection", () -> staticMethod.invoke(null, 6L));
        expectSpecialRemoved(report, "native_handle", () -> nativeHandle.invokeWithArguments(fixture, 4));
        expectSpecialRemoved(report, "static_native_handle", () -> staticHandle.invokeWithArguments(6L));
        expectSpecialRemoved(report, "native_hot_direct", () -> nativeHot.invoke(null, fixture));
        expectSpecialRemoved(report, "native_cold_direct", () -> nativeCold.invoke(null, fixture));
        expectSpecialRemoved(report, "static_native_hot_direct", () -> staticHot.invoke(null));
        expectSpecialRemoved(report, "static_native_cold_direct", () -> staticCold.invoke(null));
        int originalMode = checkJniMode(-1);
        try {
            for (int checked = 0; checked <= 1; ++checked) {
                checkJniMode(checked);
                if (checkJniMode(-1) != checked) throw new AssertionError("JNI mode");
                for (int mode = 0; mode < 4; ++mode) {
                    final int selected = mode;
                    expectSpecialRemoved(report, "constructor_jni_" + checked + "_" + mode, () -> NativeTestBridge.createSpecial(selected, 1));
                }
                for (int mode = 0; mode < 7; ++mode) {
                    final int selected = mode;
                    expectSpecialRemoved(report, "native_jni_" + checked + "_" + mode, () -> NativeTestBridge.invokeSpecialNative(selected));
                }
                if (NativeTestBridge.invokeSpecialNative(7) != 323) throw new AssertionError("Retained native binding");
            }
        } finally { checkJniMode(originalMode); }
        System.gc();
        expectSpecialRemoved(report, "constructor_after_gc", () -> constructor.newInstance(1));
        expectSpecialRemoved(report, "native_after_gc", () -> nativeMethod.invoke(fixture, 4));
        boolean state = fixture == alias && fixture.getClass() == identity && System.identityHashCode(fixture) == hash
                && fixture.value == 23 && fixture.compute() == 25 && SpecialSubject.initialized == 717
                && (Integer) kept.invoke(fixture) == 323 && ((SpecialSubject) defaultConstructor.newInstance()).value == 23;
        report.put("retainedNativeBindingAndState", state); save(report);
        if (!state) return false;
        report.put("phase", "special_members_reintroduce"); save(report);
        int restored = redefine(SpecialSubject.class, asset("special-original/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"));
        report.put("specialReintroduceResult", restored); save(report);
        if (restored != 0) return false;
        if (NativeTestBridge.invokeSpecialNative(7) != 323) throw new AssertionError("Retained binding after reintroduction");
        Constructor<?> freshCtor = SpecialSubject.class.getConstructor(int.class);
        Method freshNative = SpecialSubject.class.getMethod("nativeGone", int.class);
        boolean newBindingRequired = false;
        try { freshNative.invoke(fixture, 4); }
        catch (Throwable error) { newBindingRequired = cause(error) instanceof UnsatisfiedLinkError; }
        report.put("reintroducedNativeRequiresNewBinding", newBindingRequired); save(report);
        if (!newBindingRequired) throw new AssertionError("Retired native binding was copied to a new declaration");
        NativeTestBridge.registerSpecialNatives(SpecialSubject.class);
        boolean fresh = ((SpecialSubject) freshCtor.newInstance(4)).value == 14 && (Integer) freshNative.invoke(fixture, 4) == 127;
        report.put("specialFreshReferencesWork", fresh); save(report);
        if (!fresh) return false;
        expectSpecialRemoved(report, "old_constructor_after_reintroduce", () -> constructor.newInstance(1));
        expectSpecialRemoved(report, "old_native_after_reintroduce", () -> nativeMethod.invoke(fixture, 4));
        expectSpecialRemoved(report, "old_native_jni_after_reintroduce", () -> NativeTestBridge.invokeSpecialNative(0));
        expectSpecialRemoved(report, "old_static_native_jni_after_reintroduce", () -> NativeTestBridge.invokeSpecialNative(4));
        expectSpecialRemoved(report, "old_constructor_jni_after_reintroduce", () -> NativeTestBridge.createSpecial(1, 1));
        report.put("phase", "class_initializer_delete"); save(report);
        int noClinit = redefine(SpecialSubject.class, asset("special-clinit-delete/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"));
        report.put("classInitializerDeleteResult", noClinit); save(report);
        if (noClinit != 0) return false;
        expectFieldRemoved(report, "removed_last_initialized_static_field", () -> staticField.get(null));
        expectSpecialRemoved(report, "second_constructor_generation_removed", () -> freshCtor.newInstance(1));
        expectSpecialRemoved(report, "second_native_generation_removed", () -> freshNative.invoke(fixture, 4));
        report.put("phase", "constructor_signature_change"); save(report);
        int signature = redefine(SpecialSubject.class, asset("special-signature/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"));
        report.put("constructorSignatureResult", signature); save(report);
        if (signature != 0) return false;
        expectSpecialRemoved(report, "default_constructor_removed_by_signature_change", () -> defaultConstructor.newInstance());
        Constructor<?> changed = SpecialSubject.class.getConstructor(long.class);
        MethodHandle changedHandle = MethodHandles.lookup().findConstructor(SpecialSubject.class, MethodType.methodType(void.class, long.class));
        SpecialSubject reflectionNew = (SpecialSubject) changed.newInstance(9L);
        SpecialSubject handleNew;
        try { handleNew = (SpecialSubject) changedHandle.invokeWithArguments(11L); }
        catch (Throwable error) { throw new AssertionError("New constructor handle", error); }
        System.gc();
        expectSpecialRemoved(report, "first_constructor_after_signature_gc", () -> constructor.newInstance(1));
        expectSpecialRemoved(report, "first_native_after_signature_gc", () -> nativeMethod.invoke(fixture, 4));
        int savedMode = checkJniMode(-1);
        try {
            long wide = 9_000_000_007L;
            for (int checked = 0; checked <= 1; ++checked) {
                checkJniMode(checked);
                for (int mode = 0; mode < 4; ++mode) {
                    SpecialSubject created = (SpecialSubject) NativeTestBridge.createChangedSpecial(mode, wide);
                    if (created.value != (int) wide + 100) throw new AssertionError("New JNI constructor " + checked + "/" + mode);
                }
            }
        } finally { checkJniMode(savedMode); }
        report.put("newConstructorJniModesPassed", true); save(report);
        boolean result = reflectionNew.value == 109 && handleNew.value == 111 && fixture.value == 23
                && fixture.compute() == 27 && NativeTestBridge.invokeSpecialNative(7) == 323;
        report.put("constructorSignatureAndNativeBinding", result).put("ownedStructuralCommitCalls", commitCalls()).put("phase", "finished");
        return result && testNativeImplementation(report, fixture, kept);
    }
    private void nativeCheck(JSONObject report, String name, boolean value) throws Exception {
        report.getJSONArray("nativeImplementationChecks").put(new JSONObject().put("name", name).put("passed", value));
        save(report);
        if (!value) throw new AssertionError(name);
    }
    private void nativeValue(JSONObject report, String name, int expected, CheckedCall call) throws Exception {
        Object value;
        try { value = call.run(); }
        catch (Throwable error) { throw new AssertionError(name, cause(error)); }
        nativeCheck(report, name, value instanceof Number && ((Number) value).intValue() == expected);
    }
    private boolean testNativeImplementation(JSONObject report, SpecialSubject fixture, Method cached) throws Exception {
        report.put("nativeImplementationChecks", new JSONArray());
        MethodHandle handle = MethodHandles.lookup().findVirtual(SpecialSubject.class, "nativeKept", MethodType.methodType(int.class));
        Class<?> identity = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        nativeCheck(report, "native_modifier_baseline", Modifier.isNative(cached.getModifiers()));
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{SpecialSubject.class, Peer.class},
                new byte[][]{asset("special-java/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"), new byte[]{1, 2, 3}});
        nativeCheck(report, "invalid_native_change_batch_atomic", invalid != 0 && commitCalls() == before
                && Modifier.isNative(cached.getModifiers()) && (Integer) cached.invoke(fixture) == 323);
        // Each transition runs while the previous implementation is active.
        // The same cached Method, MethodHandle and jmethodID must keep working.
        for (int generation = 0; generation < 4; ++generation) {
            boolean toJava = generation % 2 == 0;
            String prefix = (toJava ? "native_to_java_" : "java_to_native_") + generation;
            int oldResult = toJava ? 323 : 523;
            int newResult = toJava ? 523 : 323;
            AtomicInteger activeValue = new AtomicInteger();
            AtomicReference<Throwable> activeError = new AtomicReference<>();
            DeletionControl.entered = new CountDownLatch(1);
            DeletionControl.release = new CountDownLatch(1);
            NativeTestBridge.blockKeptNative(toJava);
            Thread active = new Thread(() -> {
                try { activeValue.set((Integer) cached.invoke(fixture)); }
                catch (Throwable error) { activeError.set(cause(error)); }
            }, "native-java-transition");
            active.start();
            int code;
            try {
                if (!DeletionControl.entered.await(10, TimeUnit.SECONDS)) throw new AssertionError(prefix + " frame did not enter");
                report.put("phase", prefix + "_commit"); save(report);
                code = redefine(SpecialSubject.class, asset((toJava ? "special-java" : "special-signature") + "/com/github/ytlog/artdcevm/fixture/SpecialSubject.dex"));
            } finally {
                DeletionControl.release.countDown(); active.join(10000);
                DeletionControl.release = null; NativeTestBridge.blockKeptNative(false);
            }
            nativeCheck(report, prefix + "_commit", code == 0);
            nativeCheck(report, prefix + "_active_frame", !active.isAlive() && activeError.get() == null && activeValue.get() == oldResult);
            Method fresh = SpecialSubject.class.getMethod("nativeKept");
            nativeCheck(report, prefix + "_cached_modifiers", Modifier.isNative(cached.getModifiers()) == !toJava);
            nativeCheck(report, prefix + "_fresh_modifiers", Modifier.isNative(fresh.getModifiers()) == !toJava);
            if (!toJava) {
                Throwable caught = null;
                try { cached.invoke(fixture); } catch (Throwable error) { caught = cause(error); }
                nativeCheck(report, prefix + "_new_binding_required", caught instanceof UnsatisfiedLinkError);
                NativeTestBridge.registerKeptNative(SpecialSubject.class);
            }
            nativeValue(report, prefix + "_cached_reflection", newResult, () -> cached.invoke(fixture));
            nativeValue(report, prefix + "_fresh_reflection", newResult, () -> fresh.invoke(fixture));
            nativeValue(report, prefix + "_cached_handle", newResult, () -> handle.invokeWithArguments(fixture));
            nativeValue(report, prefix + "_direct", newResult, fixture::nativeKept);
            int savedMode = checkJniMode(-1);
            try {
                for (int checked = 0; checked <= 1; ++checked) {
                    checkJniMode(checked);
                    for (int mode = 7; mode <= 10; ++mode) {
                        final int selected = mode;
                        nativeValue(report, prefix + "_jni_" + checked + "_" + mode, newResult,
                                () -> NativeTestBridge.invokeSpecialNative(selected));
                    }
                }
            } finally { checkJniMode(savedMode); }
            System.gc();
            nativeValue(report, prefix + "_after_gc", newResult, () -> cached.invoke(fixture));
            nativeCheck(report, prefix + "_state", fixture.getClass() == identity && System.identityHashCode(fixture) == hash && fixture.value == 23);
        }
        report.put("ownedStructuralCommitCalls", commitCalls()); save(report);
        return true;
    }
    private boolean testNativeShapes(JSONObject report) throws Exception {
        NativeShape fixture = new NativeShape();
        Object marker = new byte[17], shared = new byte[19], token = new byte[23];
        fixture.marker = marker; NativeShape.shared = shared;
        Class<?> identity = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        Method wide = identity.getMethod("wide", long.class, double.class, Object.class);
        Method reference = identity.getMethod("reference", Object.class);
        Method staticReference = identity.getMethod("staticReference", Object.class);
        MethodHandle wideHandle = MethodHandles.lookup().findStatic(identity, "wide", MethodType.methodType(long.class, long.class, double.class, Object.class));
        MethodHandle referenceHandle = MethodHandles.lookup().findVirtual(identity, "reference", MethodType.methodType(Object.class, Object.class));
        MethodHandle staticHandle = MethodHandles.lookup().findStatic(identity, "staticReference", MethodType.methodType(Object.class, Object.class));
        NativeShapeProbe.register(identity); NativeShapeProbe.retain(fixture);
        long x = 9_000_000_007L;
        double y = -43.75;
        for (int generation = 0; generation <= 4; ++generation) {
            boolean java = generation % 2 == 1;
            String prefix = "native_shapes_g" + generation;
            if (generation > 0) {
                report.put("phase", prefix + "_commit"); save(report);
                int code = redefine(identity, asset((java ? "native-shape-java" : "native-shape-original") + "/com/github/ytlog/artdcevm/fixture/NativeShape.dex"));
                nativeCheck(report, prefix + "_commit", code == 0);
                if (!java) {
                    for (Method method : new Method[]{wide, reference, staticReference}) {
                        Throwable caught = null;
                        try {
                            if (method == wide) method.invoke(null, x, y, token);
                            else method.invoke(method == reference ? fixture : null, token);
                        } catch (Throwable error) { caught = cause(error); }
                        nativeCheck(report, prefix + "_new_binding_" + method.getName(), caught instanceof UnsatisfiedLinkError);
                    }
                    NativeShapeProbe.register(identity);
                }
            }
            for (Method method : new Method[]{wide, reference, staticReference})
                nativeCheck(report, prefix + "_modifier_" + method.getName(), Modifier.isNative(method.getModifiers()) != java);
            for (Object argument : new Object[]{token, null}) {
                String suffix = argument == null ? "_null" : "_object";
                long expected = x - 43 + (java ? 200 : 100) + (argument == null ? 0 : 1);
                Object expectedInstance = java ? marker : argument, expectedStatic = java ? shared : argument;
                nativeCheck(report, prefix + "_reflection_wide" + suffix, (Long) wide.invoke(null, x, y, argument) == expected);
                nativeCheck(report, prefix + "_reflection_reference" + suffix, reference.invoke(fixture, argument) == expectedInstance
                        && staticReference.invoke(null, argument) == expectedStatic);
                try {
                    nativeCheck(report, prefix + "_handles" + suffix, (long) wideHandle.invokeExact(x, y, argument) == expected
                            && referenceHandle.invokeWithArguments(fixture, argument) == expectedInstance
                            && staticHandle.invokeWithArguments(argument) == expectedStatic);
                } catch (Throwable error) { throw new AssertionError(prefix + " method handles", error); }
                int savedMode = checkJniMode(-1);
                try {
                    for (int checked = 0; checked <= 1; ++checked) {
                        checkJniMode(checked);
                        for (int mode = 0; mode <= 2; ++mode) {
                            nativeCheck(report, prefix + "_jni_wide_" + checked + "_" + mode + suffix,
                                    NativeShapeProbe.wide(mode, x, y, argument) == expected);
                            nativeCheck(report, prefix + "_jni_reference_" + checked + "_" + mode + suffix,
                                    NativeShapeProbe.reference(mode, false, argument) == expectedInstance
                                    && NativeShapeProbe.reference(mode, true, argument) == expectedStatic);
                        }
                    }
                } finally { checkJniMode(savedMode); }
                for (int i = 0; i < 10000; ++i) {
                    if (NativeShape.wide(x, y, argument) != expected || fixture.reference(argument) != expectedInstance
                            || NativeShape.staticReference(argument) != expectedStatic) throw new AssertionError(prefix + " direct warm calls");
                }
                nativeCheck(report, prefix + "_direct_warm" + suffix, true);
            }
            System.gc();
            nativeCheck(report, prefix + "_state_after_gc", fixture.getClass() == identity && System.identityHashCode(fixture) == hash
                    && fixture.marker == marker && NativeShape.shared == shared);
        }
        report.put("nativeImplementationEvolutionPassed", true).put("ownedStructuralCommitCalls", commitCalls()); save(report);
        return true;
    }
    private void storageCheck(JSONObject report, String name, boolean value) throws Exception {
        report.getJSONArray("storageChecks").put(new JSONObject().put("name", name).put("passed", value));
        save(report);
        if (!value) throw new AssertionError(name);
    }
    private void storageError(JSONObject report, String name, Class<? extends Throwable> expected,
                              CheckedCall call) throws Exception {
        report.put("phase", name); save(report);
        Throwable caught = null;
        try { call.run(); } catch (Throwable error) { caught = cause(error); }
        boolean ok = expected.isInstance(caught);
        report.getJSONArray("storageChecks").put(new JSONObject().put("name", name).put("passed", ok)
                .put("exception", caught == null ? "none" : caught.getClass().getName()));
        save(report);
        if (!ok) throw new AssertionError(name + ": expected " + expected.getName(), caught);
    }
    private Class<?> storageCaller(boolean flipped) throws Exception {
        String name = flipped ? "StorageNewCaller" : "StorageOldCaller";
        return new InMemoryDexClassLoader(ByteBuffer.wrap(asset((flipped ? "storage-flip" : "storage-original")
                + "/com/github/ytlog/artdcevm/fixture/" + name + ".dex")), StorageSubject.class.getClassLoader())
                .loadClass("com.github.ytlog.artdcevm.fixture." + name);
    }
    private void storageJni(JSONObject report, int generation, boolean flipped, boolean retired,
                            StorageSubject fixture) throws Exception {
        int originalMode = checkJniMode(-1);
        Object token = new Object();
        try {
            for (int checked = 0; checked <= 1; ++checked) {
                checkJniMode(checked);
                if (checkJniMode(-1) != checked) throw new AssertionError("CheckJNI mode mismatch");
                String prefix = "storage_jni_g" + generation + "_checked" + checked;
                for (int i = 0; i < 4; ++i) {
                    final int index = i;
                    if (retired) {
                        storageError(report, prefix + "_read" + i, NoSuchFieldError.class,
                                () -> StorageProbe.field(generation, index, false, token));
                        storageError(report, prefix + "_write" + i, NoSuchFieldError.class,
                                () -> StorageProbe.field(generation, index, true, token));
                    } else {
                        StorageProbe.field(generation, i, true, token);
                        long expected = i == 0 ? 45 : i == 1 ? 9_000_000_089L : 1;
                        storageCheck(report, prefix + "_field" + i,
                                StorageProbe.field(generation, i, false, token) == expected);
                    }
                }
                for (int kind = 0; kind < 3; ++kind) {
                    boolean statik = (kind != 1) == flipped;
                    for (int mode = 0; mode < (statik ? 3 : 4); ++mode) {
                        final int k = kind, m = mode;
                        if (retired) {
                            storageError(report, prefix + "_method" + kind + "_mode" + mode, NoSuchMethodError.class,
                                    () -> StorageProbe.invoke(generation, k, m));
                        } else {
                            long expected = kind == 0 ? (flipped ? 204 : 75)
                                    : kind == 1 ? (flipped ? 9_000_002_078L : 9_000_001_007L)
                                    : flipped ? 649 : 575;
                            storageCheck(report, prefix + "_method" + kind + "_mode" + mode,
                                    StorageProbe.invoke(generation, kind, mode) == expected);
                        }
                    }
                }
            }
        } finally { checkJniMode(originalMode); }
    }
    private boolean testStorageEvolution(JSONObject report) throws Exception {
        report.put("storageChecks", new JSONArray()).put("phase", "storage_prepare"); save(report);
        StorageSubject fixture = new StorageSubject();
        StorageSubject alias = fixture;
        StorageChild child = new StorageChild();
        Class<?> identity = fixture.getClass();
        int hash = System.identityHashCode(fixture);
        Field anchor = identity.getField("anchor");
        Method stable = identity.getMethod("stable");
        StorageProbe.registerNative(identity, false);
        StorageProbe.retain(0, fixture, false);
        storageJni(report, 0, false, false, fixture);
        Field[] fields = new Field[4];
        MethodHandle[] getters = new MethodHandle[4], setters = new MethodHandle[4];
        VarHandle[] variables = new VarHandle[4];
        String[] names = {"value", "count", "reference", "shared"};
        Class<?>[] types = {int.class, long.class, Object.class, Object.class};
        for (int i = 0; i < 4; ++i) {
            fields[i] = identity.getField(names[i]);
            getters[i] = MethodHandles.lookup().unreflectGetter(fields[i]);
            setters[i] = MethodHandles.lookup().unreflectSetter(fields[i]);
            variables[i] = i % 2 == 1 ? MethodHandles.lookup().findStaticVarHandle(identity, names[i], types[i])
                    : MethodHandles.lookup().findVarHandle(identity, names[i], types[i]);
        }
        Method oldChange = identity.getMethod("change", int.class);
        Method oldAlternate = identity.getMethod("alternate", long.class);
        Method oldNative = identity.getMethod("nativeFlip", int.class);
        MethodHandle changeHandle = MethodHandles.lookup().unreflect(oldChange);
        MethodHandle alternateHandle = MethodHandles.lookup().unreflect(oldAlternate);
        MethodHandle nativeHandle = MethodHandles.lookup().unreflect(oldNative);
        Class<?> oldCaller = storageCaller(false);
        for (int i = 0; i < 15000; ++i) {
            oldCaller.getMethod("valueHot", identity).invoke(null, fixture);
            oldCaller.getMethod("countHot", identity).invoke(null, fixture);
            oldCaller.getMethod("referenceHot", identity).invoke(null, fixture);
            oldCaller.getMethod("sharedHot", identity).invoke(null, fixture);
            for (int f = 0; f < 4; ++f)
                oldCaller.getMethod("set" + Character.toUpperCase(names[f].charAt(0)) + names[f].substring(1) + "Hot", identity, types[f])
                        .invoke(null, fixture, fields[f].get(fixture));
            oldCaller.getMethod("changeHot", identity, int.class).invoke(null, fixture, 4);
            oldCaller.getMethod("alternateHot", identity, long.class).invoke(null, fixture, 9_000_000_007L);
        }
        report.put("storagePrewarmReadBarrierCalls", readBarrierCalls()); save(report);
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{identity, Peer.class}, new byte[][]{
                asset("storage-flip/com/github/ytlog/artdcevm/fixture/StorageSubject.dex"), new byte[]{0, 1, 2}});
        storageCheck(report, "storage_invalid_batch_atomic", invalid != 0 && commitCalls() == before
                && (Integer) oldChange.invoke(fixture, 4) == 75 && StorageProbe.invoke(0, 2, 0) == 575);
        DeletionControl.entered = new CountDownLatch(2);
        DeletionControl.release = new CountDownLatch(1);
        AtomicReference<Object> activeInstance = new AtomicReference<>(), activeStatic = new AtomicReference<>();
        AtomicReference<Throwable> activeError = new AtomicReference<>();
        Thread instanceThread = new Thread(() -> {
            try { activeInstance.set(oldChange.invoke(fixture, -1)); }
            catch (Throwable error) { activeError.set(error); }
        }, "storage-old-instance");
        Thread staticThread = new Thread(() -> {
            try { activeStatic.set(oldAlternate.invoke(null, -1L)); }
            catch (Throwable error) { activeError.set(error); }
        }, "storage-old-static");
        instanceThread.start(); staticThread.start();
        int code;
        try {
            if (!DeletionControl.entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Storage frames did not enter");
            report.put("phase", "storage_flip_commit"); save(report);
            code = redefine(identity, asset("storage-flip/com/github/ytlog/artdcevm/fixture/StorageSubject.dex"));
            report.put("storageFlipResult", code); save(report);
        } finally {
            DeletionControl.release.countDown();
            instanceThread.join(10000); staticThread.join(10000);
            DeletionControl.release = null;
        }
        if (code != 0) throw new AssertionError("Storage flip returned " + code);
        storageCheck(report, "storage_active_frames_keep_old_receiver", !instanceThread.isAlive() && !staticThread.isAlive()
                && activeError.get() == null && Integer.valueOf(70).equals(activeInstance.get())
                && Long.valueOf(999).equals(activeStatic.get()));
        storageCheck(report, "storage_state_and_defaults", fixture == alias && fixture.getClass() == identity
                && System.identityHashCode(fixture) == hash && anchor.getInt(fixture) == 71
                && (Integer) stable.invoke(fixture) == 71 && child.child == 99 && child.anchor == 71
                && identity.getField("value").getInt(null) == 0
                && identity.getField("count").getLong(fixture) == 0 && identity.getField("count").getLong(child) == 0
                && identity.getField("reference").get(null) == null && identity.getField("shared").get(fixture) == null);
        for (int i = 0; i < 4; ++i) {
            final int f = i;
            boolean statik = i % 2 == 1;
            Object write = i == 0 ? (Object) 6 : i == 1 ? (Object) 9L : new Object();
            storageError(report, "storage_old_reflection_get" + i, NoSuchFieldError.class, () -> fields[f].get(fixture));
            storageError(report, "storage_old_reflection_set" + i, NoSuchFieldError.class, () -> { fields[f].set(fixture, write); return null; });
            storageError(report, "storage_old_handle_get" + i, NoSuchFieldError.class,
                    () -> statik ? getters[f].invokeWithArguments() : getters[f].invokeWithArguments(fixture));
            storageError(report, "storage_old_handle_set" + i, NoSuchFieldError.class,
                    () -> statik ? setters[f].invokeWithArguments(write) : setters[f].invokeWithArguments(fixture, write));
            storageError(report, "storage_old_varhandle" + i, NoSuchFieldError.class,
                    () -> statik ? variables[f].get() : variables[f].get(fixture));
        }
        storageError(report, "storage_old_reflection_instance", NoSuchMethodError.class, () -> oldChange.invoke(fixture, 4));
        storageError(report, "storage_old_reflection_static", NoSuchMethodError.class, () -> oldAlternate.invoke(null, 8L));
        storageError(report, "storage_old_reflection_native", NoSuchMethodError.class, () -> oldNative.invoke(fixture, 4));
        storageError(report, "storage_old_handle_instance", NoSuchMethodError.class, () -> changeHandle.invokeWithArguments(fixture, 4));
        storageError(report, "storage_old_handle_static", NoSuchMethodError.class, () -> alternateHandle.invokeWithArguments(8L));
        storageError(report, "storage_old_handle_native", NoSuchMethodError.class, () -> nativeHandle.invokeWithArguments(fixture, 4));
        storageJni(report, 0, false, true, fixture);
        for (String temperature : new String[]{"Hot", "Cold"}) {
            for (int i = 0; i < 4; ++i) {
                Method read = oldCaller.getMethod(names[i] + temperature, identity);
                Method write = oldCaller.getMethod("set" + Character.toUpperCase(names[i].charAt(0)) + names[i].substring(1) + temperature, identity, types[i]);
                Object value = i == 0 ? (Object) 6 : i == 1 ? (Object) 9L : new Object();
                storageError(report, "storage_old_bytecode_get" + i + temperature, IncompatibleClassChangeError.class, () -> read.invoke(null, fixture));
                storageError(report, "storage_old_bytecode_set" + i + temperature, IncompatibleClassChangeError.class, () -> write.invoke(null, fixture, value));
            }
            Method call = oldCaller.getMethod("change" + temperature, identity, int.class);
            Method alternate = oldCaller.getMethod("alternate" + temperature, identity, long.class);
            storageError(report, "storage_old_bytecode_instance" + temperature, IncompatibleClassChangeError.class, () -> call.invoke(null, fixture, 4));
            storageError(report, "storage_old_bytecode_static" + temperature, IncompatibleClassChangeError.class, () -> alternate.invoke(null, fixture, 8L));
            for (Method checkedCall : new Method[]{call, alternate}) {
                int hitsBefore = StorageProbe.breakpointHits();
                int installed = StorageProbe.breakpoint(checkedCall, true);
                if (installed != 0) throw new AssertionError("Switch interpreter breakpoint: " + installed);
                try {
                    Object argument = checkedCall == call ? (Object) 4 : (Object) 8L;
                    storageError(report, "storage_switch_" + checkedCall.getName(), IncompatibleClassChangeError.class,
                            () -> checkedCall.invoke(null, fixture, argument));
                    storageCheck(report, "storage_breakpoint_hit_" + checkedCall.getName(), StorageProbe.breakpointHits() > hitsBefore);
                } finally {
                    if (StorageProbe.breakpoint(checkedCall, false) != 0) throw new AssertionError("Clear breakpoint");
                }
            }
        }
        Method freshNative = identity.getMethod("nativeFlip", int.class);
        storageError(report, "storage_native_binding_not_transferred", UnsatisfiedLinkError.class, () -> freshNative.invoke(null, 4));
        StorageProbe.registerNative(identity, true);
        StorageProbe.retain(1, fixture, true);
        storageJni(report, 1, true, false, fixture);
        Class<?> freshCaller = storageCaller(true);
        storageCheck(report, "storage_new_direct_calls", (Integer) freshCaller.getMethod("changeHot", identity, int.class).invoke(null, fixture, 4) == 204
                && (Long) freshCaller.getMethod("alternateHot", identity, long.class).invoke(null, fixture, 9_000_000_007L) == 9_000_002_078L
                && (Integer) freshCaller.getMethod("valueHot", identity).invoke(null, fixture) == 45
                && (Long) freshCaller.getMethod("countHot", identity).invoke(null, fixture) == 9_000_000_089L);
        Method newChange = identity.getMethod("change", int.class);
        Field newValue = identity.getField("value");
        try {
            MethodHandle freshStatic = MethodHandles.lookup().findStatic(identity, "change", MethodType.methodType(int.class, int.class));
            MethodHandle freshVirtual = MethodHandles.lookup().findVirtual(identity, "alternate", MethodType.methodType(long.class, long.class));
            VarHandle staticInt = MethodHandles.lookup().findStaticVarHandle(identity, "value", int.class);
            VarHandle instanceLong = MethodHandles.lookup().findVarHandle(identity, "count", long.class);
            VarHandle staticRef = MethodHandles.lookup().findStaticVarHandle(identity, "reference", Object.class);
            VarHandle instanceRef = MethodHandles.lookup().findVarHandle(identity, "shared", Object.class);
            Object token = new Object();
            staticRef.set(token); instanceRef.set(fixture, token);
            storageCheck(report, "storage_new_handles_receiver_types", (int) freshStatic.invokeExact(4) == 204
                    && (long) freshVirtual.invokeExact(fixture, 9_000_000_007L) == 9_000_002_078L
                    && (int) staticInt.get() == 45 && (long) instanceLong.get(fixture) == 9_000_000_089L
                    && staticRef.get() == token && instanceRef.get(fixture) == token
                    && staticInt.compareAndSet(45, 49) && instanceLong.compareAndSet(fixture, 9_000_000_089L, 99L));
        } catch (Throwable error) { throw new AssertionError("New storage handles", error); }
        report.put("phase", "storage_restore_commit"); save(report);
        code = redefine(identity, asset("storage-original/com/github/ytlog/artdcevm/fixture/StorageSubject.dex"));
        report.put("storageRestoreResult", code); save(report);
        if (code != 0) throw new AssertionError("Storage restore returned " + code);
        System.gc();
        storageCheck(report, "storage_restored_defaults_and_identity", fixture == alias && fixture.getClass() == identity
                && fixture.value == 0 && child.value == 0 && StorageSubject.count == 0 && fixture.reference == null
                && StorageSubject.shared == null && fixture.anchor == 71 && child.child == 99);
        storageError(report, "storage_first_generation_stays_retired", NoSuchMethodError.class, () -> oldChange.invoke(fixture, 4));
        storageError(report, "storage_first_field_stays_retired", NoSuchFieldError.class, () -> fields[0].get(fixture));
        storageError(report, "storage_second_generation_retired", NoSuchMethodError.class, () -> newChange.invoke(null, 4));
        storageError(report, "storage_second_field_retired", NoSuchFieldError.class, () -> newValue.get(null));
        storageJni(report, 0, false, true, fixture);
        storageJni(report, 1, true, true, fixture);
        storageError(report, "storage_restored_native_requires_new_binding", UnsatisfiedLinkError.class,
                () -> identity.getMethod("nativeFlip", int.class).invoke(fixture, 4));
        StorageProbe.registerNative(identity, false);
        storageCheck(report, "storage_fresh_restored_calls", (Integer) identity.getMethod("change", int.class).invoke(fixture, 4) == 75
                && (Integer) identity.getMethod("nativeFlip", int.class).invoke(fixture, 4) == 575);
        report.put("phase", "storage_concurrent_gc"); save(report);
        Field[] gcFields = new Field[4096];
        for (int i = 0; i < gcFields.length; ++i) gcFields[i] = identity.getField("reference");
        long barriersBefore = readBarrierCalls();
        long collectionsBefore = Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count"));
        if (StorageProbe.gcStress(0) != 0) throw new AssertionError("GC event initialization failed");
        AtomicInteger gcError = new AtomicInteger();
        CountDownLatch stressStart = new CountDownLatch(1);
        Thread collector = new Thread(() -> {
            try { stressStart.await(); } catch (InterruptedException error) { return; }
            for (int i = 0; i < 24; ++i) {
                int result = StorageProbe.gcStress(1);
                if (result != 0) { gcError.set(result); break; }
            }
        }, "source-guard-gc-stress");
        collector.start(); stressStart.countDown();
        try {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
            for (int i = 0; (i < 30000 || collector.isAlive()) && System.nanoTime() < deadline; ++i) {
                Object token = new byte[1024];
                Field reference = gcFields[i % gcFields.length];
                reference.set(fixture, token);
                if (reference.get(fixture) != token) throw new AssertionError("Reference changed during GC");
            }
        } finally { collector.join(10000); }
        long barrierDelta = readBarrierCalls() - barriersBefore;
        long collectionDelta = Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count")) - collectionsBefore;
        // Do not dispose an environment still in use by a collector thread.
        int finishedCollections = collector.isAlive() ? -1 : StorageProbe.gcStress(2);
        report.put("concurrentGcReadBarrierCalls", barrierDelta).put("concurrentGcCollections", collectionDelta)
                .put("concurrentGcFinishEvents", finishedCollections).put("concurrentGcError", gcError.get());
        storageCheck(report, "storage_guards_under_concurrent_gc", !collector.isAlive() && gcError.get() == 0
                && barrierDelta > 0 && finishedCollections > 0);
        int modeBeforeGcChecks = checkJniMode(-1);
        try {
            Object token = new byte[29];
            for (int checked = 0; checked <= 1; ++checked) {
                checkJniMode(checked);
                nativeCheck(report, "native_cached_ids_after_verified_gc_" + checked,
                        NativeTestBridge.invokeSpecialNative(7) == 323
                        && NativeShapeProbe.wide(1, 9_000_000_007L, -43.75, token) == 9_000_000_065L
                        && NativeShapeProbe.reference(1, false, token) == token
                        && NativeShapeProbe.reference(1, true, token) == token);
            }
        } finally { checkJniMode(modeBeforeGcChecks); }
        report.put("memberStorageEvolutionPassed", true).put("ownedStructuralCommitCalls", commitCalls()).put("phase", "finished");
        return true;
    }
    private byte[] patch(String variant) throws Exception {
        return asset(variant + "/com/github/ytlog/artdcevm/fixture/Evolving.dex");
    }
    private void modifierCheck(JSONObject report, String name, boolean passed) throws Exception {
        report.getJSONArray("modifierChecks").put(new JSONObject().put("name", name).put("passed", passed));
        save(report);
        if (!passed) throw new AssertionError(name);
    }
    private boolean testPrivateMethodTransition(JSONObject report) throws Exception {
        if (!report.has("modifierChecks")) report.put("modifierChecks", new JSONArray());
        report.put("phase", "private_method_baseline"); save(report);
        modifierCheck(report, "private_method_capability",
                backend.capabilities().getBoolean("privatePublicInstanceMethodTransition"));
        PrivateMethodSubject subject = new PrivateMethodSubject();
        Class<?> identity = subject.getClass();
        Method cached = identity.getDeclaredMethod("compute");
        cached.setAccessible(true);
        modifierCheck(report, "private_method_baseline", Modifier.isPrivate(cached.getModifiers())
                && subject.callOwn() == 29 && (Integer) cached.invoke(subject) == 29);
        for (int i = 0; i < 100000; ++i) sink ^= PrivateMethodHotCaller.call(subject);
        Method hotCallerMethod = PrivateMethodHotCaller.class.getMethod("call", PrivateMethodSubject.class);
        int hotCallerJitState = jitMethodState(hotCallerMethod);
        for (int attempt = 0; attempt < 20 && hotCallerJitState != 2
                && !allMethodsDeoptimized(); ++attempt) {
            for (int i = 0; i < 20000; ++i) sink ^= PrivateMethodHotCaller.call(subject);
            Thread.sleep(20);
            hotCallerJitState = jitMethodState(hotCallerMethod);
        }
        report.put("privateCallOwnJitStateBefore", jitMethodState(identity.getMethod("callOwn")))
                .put("privateHotCallerJitStateBefore", hotCallerJitState)
                .put("privateDeoptimizedBefore", allMethodsDeoptimized()); save(report);
        modifierCheck(report, "private_hot_caller_compiled_or_deoptimized",
                (allMethodsDeoptimized() || hotCallerJitState == 2)
                && PrivateMethodHotCaller.call(subject) == 29);
        int before = commitCalls();
        int publicCode = redefine(identity, asset("private-method-public/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("privateToPublicResult", publicCode); save(report);
        modifierCheck(report, "private_to_public_commit", publicCode == 0 && commitCalls() == before + 1);
        modifierCheck(report, "private_to_public_calls", subject.callOwn() == 40
                && PrivateMethodHotCaller.call(subject) == 40
                && (Integer) cached.invoke(subject) == 40
                && (Integer) identity.getMethod("compute").invoke(subject) == 40
                && Modifier.isPublic(cached.getModifiers()));
        before = commitCalls();
        int privateCode = redefine(identity, asset("private-method-private/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("publicToPrivateResult", privateCode); save(report);
        modifierCheck(report, "public_to_private_commit", privateCode == 0 && commitCalls() == before + 1);
        modifierCheck(report, "public_to_private_calls", subject.callOwn() == 31
                && PrivateMethodHotCaller.call(subject) == 31
                && (Integer) cached.invoke(subject) == 31
                && Modifier.isPrivate(cached.getModifiers())
                && Modifier.isPrivate(identity.getDeclaredMethod("compute").getModifiers()));
        Class<?> childClass = Class.forName("com.github.ytlog.artdcevm.fixture.PrivateMethodChild");
        Object child = childClass.getConstructor().newInstance();
        Method childCallOwn = childClass.getMethod("callOwn");
        Method childCompute = childClass.getMethod("compute");
        Class<?> passiveClass = Class.forName("com.github.ytlog.artdcevm.fixture.PrivateMethodPassive");
        Object passive = passiveClass.getConstructor().newInstance();
        Method passiveCallOwn = passiveClass.getMethod("callOwn");
        modifierCheck(report, "private_loaded_child_baseline",
                (Integer) childCallOwn.invoke(child) == 31
                && (Integer) childCompute.invoke(child) == 53
                && (Integer) passiveCallOwn.invoke(passive) == 31
                && PrivateMethodHotCaller.call((PrivateMethodSubject) child) == 31
                && PrivateMethodHotCaller.call((PrivateMethodSubject) passive) == 31);
        before = commitCalls();
        int conflict = redefine(identity, asset("private-method-public/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("privateLoadedChildResult", conflict); save(report);
        modifierCheck(report, "private_loaded_child_public_commit", conflict == 0
                && commitCalls() == before + 1 && (Integer) childCallOwn.invoke(child) == 53
                && (Integer) childCompute.invoke(child) == 53
                && (Integer) passiveCallOwn.invoke(passive) == 40
                && (Integer) cached.invoke(subject) == 40 && subject.callOwn() == 40
                && PrivateMethodHotCaller.call((PrivateMethodSubject) child) == 53
                && PrivateMethodHotCaller.call((PrivateMethodSubject) passive) == 40);
        before = commitCalls();
        int privateAgain = redefine(identity, asset("private-method-private/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("privateLoadedChildReverseResult", privateAgain); save(report);
        modifierCheck(report, "private_loaded_child_reverse_commit", privateAgain == 0
                && commitCalls() == before + 1 && (Integer) childCallOwn.invoke(child) == 31
                && (Integer) childCompute.invoke(child) == 53
                && (Integer) passiveCallOwn.invoke(passive) == 31
                && (Integer) cached.invoke(subject) == 31 && subject.callOwn() == 31
                && PrivateMethodHotCaller.call((PrivateMethodSubject) child) == 31
                && PrivateMethodHotCaller.call((PrivateMethodSubject) passive) == 31);
        return true;
    }
    private Class<?> privateFieldCaller() throws Exception {
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "private-field-external/com/github/ytlog/artdcevm/external/PrivateFieldCaller.dex")),
                PrivateFieldSubject.class.getClassLoader());
        return loader.loadClass("com.github.ytlog.artdcevm.external.PrivateFieldCaller");
    }
    private boolean privateFieldExternalDenied(PrivateFieldSubject subject) throws Exception {
        try {
            privateFieldCaller().getMethod("read", PrivateFieldSubject.class).invoke(null, subject);
            return false;
        } catch (InvocationTargetException error) {
            return error.getCause() instanceof IllegalAccessError;
        }
    }
    private boolean testPrivateFieldTransition(JSONObject report) throws Exception {
        if (!report.has("modifierChecks")) report.put("modifierChecks", new JSONArray());
        report.put("phase", "private_field_baseline"); save(report);
        modifierCheck(report, "private_field_capability",
                backend.capabilities().getBoolean("privateFieldVisibilityTransition"));
        PrivateFieldSubject subject = new PrivateFieldSubject();
        Class<?> identity = subject.getClass();
        Field cachedInstance = identity.getDeclaredField("value");
        Field cachedStatic = identity.getDeclaredField("shared");
        cachedInstance.setAccessible(true); cachedStatic.setAccessible(true);
        subject.write(21); PrivateFieldSubject.writeShared(23);
        modifierCheck(report, "private_field_baseline", Modifier.isPrivate(cachedInstance.getModifiers())
                && Modifier.isPrivate(cachedStatic.getModifiers())
                && cachedInstance.getInt(subject) == 21 && cachedStatic.getInt(null) == 23
                && PrivateFieldHotCaller.read(subject) == 44
                && privateFieldExternalDenied(subject));
        for (int i = 0; i < 100000; ++i) sink ^= PrivateFieldHotCaller.read(subject);
        Method hotCaller = PrivateFieldHotCaller.class.getMethod("read", PrivateFieldSubject.class);
        int jit = jitMethodState(hotCaller);
        for (int attempt = 0; attempt < 20 && jit != 2 && !allMethodsDeoptimized(); ++attempt) {
            for (int i = 0; i < 20000; ++i) sink ^= PrivateFieldHotCaller.read(subject);
            Thread.sleep(20);
            jit = jitMethodState(hotCaller);
        }
        report.put("privateFieldHotCallerJitStateBefore", jit)
                .put("privateFieldDeoptimizedBefore", allMethodsDeoptimized()); save(report);
        modifierCheck(report, "private_field_hot_caller_compiled_or_deoptimized",
                (jit == 2 || allMethodsDeoptimized()) && PrivateFieldHotCaller.read(subject) == 44);
        int before = commitCalls();
        int publicCode = redefine(identity, asset("private-field-public/com/github/ytlog/artdcevm/fixture/PrivateFieldSubject.dex"));
        report.put("privateFieldPublicResult", publicCode); save(report);
        modifierCheck(report, "private_field_public_commit", publicCode == 0
                && commitCalls() == before + 1 && Modifier.isPublic(cachedInstance.getModifiers())
                && Modifier.isPublic(cachedStatic.getModifiers())
                && Modifier.isPublic(identity.getField("value").getModifiers())
                && Modifier.isPublic(identity.getField("shared").getModifiers())
                && cachedInstance.getInt(subject) == 21 && cachedStatic.getInt(null) == 23
                && PrivateFieldHotCaller.read(subject) == 44);
        Class<?> caller = privateFieldCaller();
        Method externalRead = caller.getMethod("read", PrivateFieldSubject.class);
        Method externalWrite = caller.getMethod("write", PrivateFieldSubject.class, int.class, int.class);
        modifierCheck(report, "private_field_external_public", (Integer) externalRead.invoke(null, subject) == 44);
        externalWrite.invoke(null, subject, 41, 43);
        modifierCheck(report, "private_field_external_write", cachedInstance.getInt(subject) == 41
                && cachedStatic.getInt(null) == 43 && PrivateFieldHotCaller.read(subject) == 84);
        before = commitCalls();
        int privateCode = redefine(identity, asset("private-field-private/com/github/ytlog/artdcevm/fixture/PrivateFieldSubject.dex"));
        report.put("privateFieldReverseResult", privateCode); save(report);
        modifierCheck(report, "private_field_reverse_commit", privateCode == 0
                && commitCalls() == before + 1 && Modifier.isPrivate(cachedInstance.getModifiers())
                && Modifier.isPrivate(cachedStatic.getModifiers())
                && cachedInstance.getInt(subject) == 41 && cachedStatic.getInt(null) == 43
                && PrivateFieldHotCaller.read(subject) == 84);
        modifierCheck(report, "private_field_fresh_external_denied", privateFieldExternalDenied(subject));
        return true;
    }
    private boolean testPrivateVisibilityVariants(JSONObject report) throws Exception {
        if (!report.has("modifierChecks")) report.put("modifierChecks", new JSONArray());
        report.put("phase", "private_visibility_variants"); save(report);
        PrivateMethodSubject methodSubject = new PrivateMethodSubject();
        int privateBaseline = methodSubject.callOwn();
        Class<?> methodClass = methodSubject.getClass();
        Method cachedMethod = methodClass.getDeclaredMethod("compute");
        cachedMethod.setAccessible(true);
        Class<?> childClass = Class.forName("com.github.ytlog.artdcevm.fixture.PrivateMethodChild");
        Object child = childClass.getConstructor().newInstance();
        Method childCallOwn = childClass.getMethod("callOwn");
        Method childCompute = childClass.getMethod("compute");
        Class<?> passiveClass = Class.forName("com.github.ytlog.artdcevm.fixture.PrivateMethodPassive");
        Object passive = passiveClass.getConstructor().newInstance();
        Method passiveCallOwn = passiveClass.getMethod("callOwn");
        PrivateMethodExternalChild externalChild = new PrivateMethodExternalChild();
        modifierCheck(report, "private_visibility_method_baseline",
                (privateBaseline == 29 || privateBaseline == 31)
                        && (Integer) childCallOwn.invoke(child) == privateBaseline
                        && (Integer) childCompute.invoke(child) == 53
                        && (Integer) passiveCallOwn.invoke(passive) == privateBaseline
                        && externalChild.callOwn() == privateBaseline
                        && externalChild.compute() == 61
                        && Modifier.isPrivate(cachedMethod.getModifiers()));
        int protectedCode = redefine(methodClass, asset(
                "private-method-protected/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("privateToProtectedMethodResult", protectedCode); save(report);
        modifierCheck(report, "private_to_protected_method_dispatch",
                protectedCode == 0 && methodSubject.callOwn() == 42
                        && (Integer) cachedMethod.invoke(methodSubject) == 42
                        && (Integer) childCallOwn.invoke(child) == 53
                        && (Integer) passiveCallOwn.invoke(passive) == 42
                        && externalChild.callOwn() == 61
                        && externalChild.compute() == 61
                        && Modifier.isProtected(cachedMethod.getModifiers()));
        int packageCode = redefine(methodClass, asset(
                "private-method-package/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("protectedToPackageMethodResult", packageCode); save(report);
        modifierCheck(report, "protected_to_package_method_dispatch",
                packageCode == 0 && methodSubject.callOwn() == 43
                        && (Integer) cachedMethod.invoke(methodSubject) == 43
                        && (Integer) childCallOwn.invoke(child) == 53
                        && (Integer) passiveCallOwn.invoke(passive) == 43
                        && externalChild.callOwn() == 43
                        && externalChild.compute() == 61
                        && (cachedMethod.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED
                                | Modifier.PRIVATE)) == 0);
        int privateCode = redefine(methodClass, asset(
                "private-method-private/com/github/ytlog/artdcevm/fixture/PrivateMethodSubject.dex"));
        report.put("packageToPrivateMethodResult", privateCode); save(report);
        modifierCheck(report, "package_to_private_method_dispatch",
                privateCode == 0 && methodSubject.callOwn() == 31
                        && (Integer) cachedMethod.invoke(methodSubject) == 31
                        && (Integer) childCallOwn.invoke(child) == 31
                        && (Integer) childCompute.invoke(child) == 53
                        && (Integer) passiveCallOwn.invoke(passive) == 31
                        && externalChild.callOwn() == 31
                        && externalChild.compute() == 61
                        && Modifier.isPrivate(cachedMethod.getModifiers()));

        PrivateFieldSubject fieldSubject = new PrivateFieldSubject();
        Class<?> fieldClass = fieldSubject.getClass();
        Field cachedValue = fieldClass.getDeclaredField("value");
        Field cachedShared = fieldClass.getDeclaredField("shared");
        cachedValue.setAccessible(true); cachedShared.setAccessible(true);
        fieldSubject.write(21); PrivateFieldSubject.writeShared(23);
        int fieldProtectedCode = redefine(fieldClass, asset(
                "private-field-protected/com/github/ytlog/artdcevm/fixture/PrivateFieldSubject.dex"));
        report.put("privateToProtectedFieldResult", fieldProtectedCode); save(report);
        modifierCheck(report, "private_to_protected_field_storage",
                fieldProtectedCode == 0 && Modifier.isProtected(cachedValue.getModifiers())
                        && Modifier.isProtected(cachedShared.getModifiers())
                        && cachedValue.getInt(fieldSubject) == 21 && cachedShared.getInt(null) == 23
                        && PrivateFieldHotCaller.read(fieldSubject) == 44
                        && privateFieldExternalDenied(fieldSubject));
        int fieldPackageCode = redefine(fieldClass, asset(
                "private-field-package/com/github/ytlog/artdcevm/fixture/PrivateFieldSubject.dex"));
        report.put("protectedToPackageFieldResult", fieldPackageCode); save(report);
        modifierCheck(report, "protected_to_package_field_storage",
                fieldPackageCode == 0
                        && (cachedValue.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED
                                | Modifier.PRIVATE)) == 0
                        && (cachedShared.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED
                                | Modifier.PRIVATE)) == 0
                        && cachedValue.getInt(fieldSubject) == 21 && cachedShared.getInt(null) == 23
                        && PrivateFieldHotCaller.read(fieldSubject) == 44
                        && privateFieldExternalDenied(fieldSubject));
        int fieldPrivateCode = redefine(fieldClass, asset(
                "private-field-private/com/github/ytlog/artdcevm/fixture/PrivateFieldSubject.dex"));
        report.put("packageToPrivateFieldResult", fieldPrivateCode); save(report);
        modifierCheck(report, "package_to_private_field_storage",
                fieldPrivateCode == 0 && Modifier.isPrivate(cachedValue.getModifiers())
                        && Modifier.isPrivate(cachedShared.getModifiers())
                        && cachedValue.getInt(fieldSubject) == 21 && cachedShared.getInt(null) == 23
                        && PrivateFieldHotCaller.read(fieldSubject) == 44
                        && privateFieldExternalDenied(fieldSubject));
        return true;
    }
    private Class<?> privateStaticExternalCaller() throws Exception {
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "private-static-external/com/github/ytlog/artdcevm/external/PrivateStaticMethodCaller.dex")),
                PrivateStaticMethodSubject.class.getClassLoader());
        return loader.loadClass("com.github.ytlog.artdcevm.external.PrivateStaticMethodCaller");
    }
    private boolean privateStaticExternalDenied() throws Exception {
        try {
            privateStaticExternalCaller().getMethod("call").invoke(null);
            return false;
        } catch (InvocationTargetException error) {
            return error.getCause() instanceof IllegalAccessError;
        }
    }
    private boolean testPrivateStaticVisibility(JSONObject report) throws Exception {
        if (!report.has("modifierChecks")) report.put("modifierChecks", new JSONArray());
        report.put("phase", "private_static_visibility")
                .put("privateStaticDeoptimizedBefore", allMethodsDeoptimized()); save(report);
        Class<?> type = PrivateStaticMethodSubject.class;
        Method cached = type.getDeclaredMethod("compute");
        cached.setAccessible(true);
        modifierCheck(report, "private_static_baseline",
                PrivateStaticMethodSubject.callOwn() == 71
                        && (Integer) cached.invoke(null) == 71
                        && Modifier.isPrivate(cached.getModifiers())
                        && Modifier.isStatic(cached.getModifiers())
                        && privateStaticExternalDenied());
        int protectedCode = redefine(type, asset(
                "private-static-method-protected/com/github/ytlog/artdcevm/fixture/PrivateStaticMethodSubject.dex"));
        report.put("privateStaticToProtectedResult", protectedCode); save(report);
        modifierCheck(report, "private_static_to_protected",
                protectedCode == 0 && PrivateStaticMethodSubject.callOwn() == 72
                        && (Integer) cached.invoke(null) == 72
                        && Modifier.isProtected(cached.getModifiers())
                        && Modifier.isStatic(cached.getModifiers())
                        && privateStaticExternalDenied());
        int packageCode = redefine(type, asset(
                "private-static-method-package/com/github/ytlog/artdcevm/fixture/PrivateStaticMethodSubject.dex"));
        report.put("protectedStaticToPackageResult", packageCode); save(report);
        modifierCheck(report, "protected_static_to_package",
                packageCode == 0 && PrivateStaticMethodSubject.callOwn() == 73
                        && (Integer) cached.invoke(null) == 73
                        && (cached.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED
                                | Modifier.PRIVATE)) == 0 && Modifier.isStatic(cached.getModifiers())
                        && privateStaticExternalDenied());
        int publicCode = redefine(type, asset(
                "private-static-method-public/com/github/ytlog/artdcevm/fixture/PrivateStaticMethodSubject.dex"));
        report.put("packageStaticToPublicResult", publicCode); save(report);
        Class<?> oldExternalCaller = privateStaticExternalCaller();
        Method oldExternalCall = oldExternalCaller.getMethod("call");
        MethodHandle oldExternalHandle = MethodHandles.publicLookup().findStatic(type,
                "compute", MethodType.methodType(int.class));
        Object oldHandleBaseline;
        try { oldHandleBaseline = oldExternalHandle.invokeWithArguments(); }
        catch (Throwable error) { throw new AssertionError("Old public static MethodHandle", error); }
        boolean jniRetained = StaticVisibilityProbe.retain(type);
        modifierCheck(report, "package_static_to_public",
                publicCode == 0 && PrivateStaticMethodSubject.callOwn() == 74
                        && (Integer) cached.invoke(null) == 74
                        && Modifier.isPublic(cached.getModifiers())
                        && Modifier.isStatic(cached.getModifiers())
                        && (Integer) oldExternalCall.invoke(null) == 74
                        && Integer.valueOf(74).equals(oldHandleBaseline)
                        && jniRetained && StaticVisibilityProbe.invoke(0, false) == 74);
        int externalJit = jitMethodState(oldExternalCall);
        for (int attempt = 0; attempt < 12 && externalJit != 2
                && !allMethodsDeoptimized(); ++attempt) {
            for (int i = 0; i < 50000; ++i) sink ^= (Integer) oldExternalCall.invoke(null);
            Thread.sleep(100);
            externalJit = jitMethodState(oldExternalCall);
        }
        report.put("privateStaticExternalJitBeforeNarrow", externalJit)
                .put("privateStaticExternalDeoptimizedBeforeNarrow", allMethodsDeoptimized()); save(report);
        modifierCheck(report, "private_static_external_compiled_or_deoptimized",
                allMethodsDeoptimized() || externalJit == 2);
        int privateCode = redefine(type, asset(
                "private-static-method-private/com/github/ytlog/artdcevm/fixture/PrivateStaticMethodSubject.dex"));
        report.put("publicStaticToPrivateResult", privateCode); save(report);
        modifierCheck(report, "public_static_to_private",
                privateCode == 0 && PrivateStaticMethodSubject.callOwn() == 75
                        && (Integer) cached.invoke(null) == 75
                        && Modifier.isPrivate(cached.getModifiers())
                        && Modifier.isStatic(cached.getModifiers())
                        && privateStaticExternalDenied());
        Object oldOutcome;
        try { oldOutcome = oldExternalCall.invoke(null); }
        catch (InvocationTargetException error) { oldOutcome = error.getCause().getClass().getName(); }
        report.put("privateStaticOldExternalCallAfterNarrow", oldOutcome); save(report);
        modifierCheck(report, "private_static_old_external_denied_after_narrow",
                IllegalAccessError.class.getName().equals(oldOutcome));
        Object oldHandleOutcome;
        try { oldHandleOutcome = oldExternalHandle.invokeWithArguments(); }
        catch (Throwable error) { oldHandleOutcome = error.getClass().getName(); }
        report.put("privateStaticOldHandleAfterNarrow", oldHandleOutcome); save(report);
        modifierCheck(report, "private_static_old_handle_no_stale_body",
                Integer.valueOf(75).equals(oldHandleOutcome));
        boolean freshHandleDenied = false;
        try { MethodHandles.publicLookup().findStatic(type, "compute", MethodType.methodType(int.class)); }
        catch (IllegalAccessException expected) { freshHandleDenied = true; }
        modifierCheck(report, "private_static_fresh_public_handle_denied", freshHandleDenied);
        int previousJniMode = checkJniMode(-1);
        try {
            for (int checked = 0; checked <= 1; ++checked) {
                checkJniMode(checked);
                int[] oldJni = new int[3], freshJni = new int[3];
                for (int mode = 0; mode < 3; ++mode) {
                    oldJni[mode] = StaticVisibilityProbe.invoke(mode, false);
                    freshJni[mode] = StaticVisibilityProbe.invoke(mode, true);
                }
                report.put("privateStaticJniAfterNarrowCheck" + checked,
                        new JSONObject().put("old", java.util.Arrays.toString(oldJni))
                                .put("fresh", java.util.Arrays.toString(freshJni))); save(report);
                modifierCheck(report, "private_static_jni_cached_and_fresh_check" + checked,
                        checkJniMode(-1) == checked
                                && oldJni[0] == 75 && oldJni[1] == 75 && oldJni[2] == 75
                                && freshJni[0] == 75 && freshJni[1] == 75 && freshJni[2] == 75);
            }
        } finally {
            checkJniMode(previousJniMode);
            StaticVisibilityProbe.release();
        }
        return true;
    }
    @FunctionalInterface private interface ModifierCall { int invoke() throws Throwable; }
    private boolean modifierMonitorCheck(Object monitor, ModifierCall invoke,
                                         boolean expectBlocked, int expected) throws Exception {
        CountDownLatch started = new CountDownLatch(1);
        AtomicInteger result = new AtomicInteger(Integer.MIN_VALUE);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread caller = new Thread(() -> {
            started.countDown();
            try { result.set(invoke.invoke()); }
            catch (Throwable error) { failure.set(error); }
        }, "modifier-monitor-caller");
        boolean observed;
        synchronized (monitor) {
            caller.start();
            if (!started.await(2, TimeUnit.SECONDS)) throw new AssertionError("Caller did not start");
            if (expectBlocked) {
                long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
                while (caller.getState() != Thread.State.BLOCKED && caller.isAlive()
                        && System.nanoTime() < deadline) Thread.sleep(5);
                observed = caller.getState() == Thread.State.BLOCKED
                        && result.get() == Integer.MIN_VALUE;
            } else {
                caller.join(2000);
                observed = !caller.isAlive() && result.get() == expected;
            }
        }
        caller.join(2000);
        return observed && !caller.isAlive() && failure.get() == null && result.get() == expected;
    }
    private String modifierAccessOutcome(Method caller, ModifierSubject subject) {
        try { return "returned:" + caller.invoke(null, subject); }
        catch (InvocationTargetException error) {
            return error.getCause().getClass().getName();
        } catch (IllegalAccessException error) { return error.getClass().getName(); }
    }
    private boolean testVisibilityWidening(JSONObject report) throws Exception {
        report.put("modifierChecks", new JSONArray()).put("phase", "modifier_baseline"); save(report);
        modifierCheck(report, "visibility_narrowing_capability",
                backend.capabilities().getBoolean("memberVisibilityNarrowing"));
        ModifierSubject instance = new ModifierSubject();
        ModifierPassive passive = new ModifierPassive();
        Class<?> identity = instance.getClass();
        Field cachedField = identity.getDeclaredField("value");
        Method cachedMethod = identity.getDeclaredMethod("compute");
        Method cachedStaticMethod = identity.getDeclaredMethod("staticCompute");
        Method cachedFinalMethod = identity.getDeclaredMethod("finalizable");
        Field cachedPackageField = identity.getDeclaredField("packageValue");
        Method cachedPackageMethod = identity.getDeclaredMethod("packageCompute");
        cachedField.setAccessible(true); cachedMethod.setAccessible(true);
        cachedPackageField.setAccessible(true); cachedPackageMethod.setAccessible(true);
        modifierCheck(report, "visibility_baseline", Modifier.isProtected(cachedField.getModifiers())
                && Modifier.isProtected(cachedMethod.getModifiers())
                && (cachedPackageField.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED | Modifier.PRIVATE)) == 0
                && (cachedPackageMethod.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED | Modifier.PRIVATE)) == 0
                && instance.throughPublic() == 42 && (Integer) cachedMethod.invoke(instance) == 42
                && Modifier.isStatic(cachedStaticMethod.getModifiers())
                && !Modifier.isSynchronized(cachedStaticMethod.getModifiers())
                && ModifierSubject.staticCompute() == 19
                && !Modifier.isFinal(cachedFinalMethod.getModifiers())
                && instance.finalizable() == 29
                && passive.finalizable() == 29
                && (Integer) cachedPackageMethod.invoke(instance) == 12);
        long before = commitCalls();
        int code = redefine(identity, asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("visibilityWideningResult", code); save(report);
        modifierCheck(report, "visibility_widening_commit", code == 0 && commitCalls() == before + 1
                && instance.getClass() == identity && instance.throughPublic() == 43);
        Field freshField = identity.getField("value");
        Method freshMethod = identity.getMethod("compute");
        Field freshPackageField = identity.getField("packageValue");
        Method freshPackageMethod = identity.getMethod("packageCompute");
        modifierCheck(report, "visibility_reflection_flags", Modifier.isPublic(freshField.getModifiers())
                && Modifier.isPublic(freshMethod.getModifiers())
                && Modifier.isPublic(cachedField.getModifiers()) && Modifier.isPublic(cachedMethod.getModifiers())
                && Modifier.isPublic(freshPackageField.getModifiers()) && Modifier.isPublic(freshPackageMethod.getModifiers())
                && Modifier.isPublic(cachedPackageField.getModifiers()) && Modifier.isPublic(cachedPackageMethod.getModifiers()));
        freshField.setInt(instance, 73);
        freshPackageField.setInt(instance, 23);
        MethodHandle freshHandle = MethodHandles.publicLookup().findVirtual(identity, "compute", MethodType.methodType(int.class));
        MethodHandle staticHandle = MethodHandles.publicLookup().findStatic(identity,
                "staticCompute", MethodType.methodType(int.class));
        MethodHandle finalHandle = MethodHandles.publicLookup().findVirtual(identity,
                "finalizable", MethodType.methodType(int.class));
        MethodHandle packageHandle = MethodHandles.publicLookup().findVirtual(identity, "packageCompute", MethodType.methodType(int.class));
        try {
            modifierCheck(report, "visibility_state_and_calls", cachedField.getInt(instance) == 73
                    && cachedPackageField.getInt(instance) == 23
                    && (Integer) cachedMethod.invoke(instance) == 75
                    && (Integer) freshMethod.invoke(instance) == 75
                    && (int) freshHandle.invokeWithArguments(instance) == 75
                    && (Integer) cachedPackageMethod.invoke(instance) == 31
                    && (Integer) freshPackageMethod.invoke(instance) == 31
                    && (int) packageHandle.invokeWithArguments(instance) == 31
                    && instance.throughPublic() == 75);
        } catch (Throwable error) { throw new AssertionError("Public method handle", error); }
        ClassLoader externalLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalCaller.dex")), identity.getClassLoader());
        Class<?> external = Class.forName("com.github.ytlog.artdcevm.external.ModifierExternalCaller", true, externalLoader);
        Method externalField = external.getMethod("field", ModifierSubject.class);
        Method externalMethod = external.getMethod("method", ModifierSubject.class);
        Method externalPackageField = external.getMethod("packageField", ModifierSubject.class);
        Method externalPackageMethod = external.getMethod("packageMethod", ModifierSubject.class);
        modifierCheck(report, "visibility_external_public_baseline",
                (Integer) externalField.invoke(null, instance) == 73
                && (Integer) externalMethod.invoke(null, instance) == 75
                && (Integer) externalPackageField.invoke(null, instance) == 23
                && (Integer) externalPackageMethod.invoke(null, instance) == 31);
        for (int i = 0; i < 100000; ++i) sink ^= (Integer) externalMethod.invoke(null, instance);
        int externalJit = jitMethodState(externalMethod);
        for (int attempt = 0; attempt < 12 && externalJit != 2; ++attempt) {
            for (int i = 0; i < 50000; ++i) sink ^= (Integer) externalMethod.invoke(null, instance);
            Thread.sleep(100);
            externalJit = jitMethodState(externalMethod);
        }
        report.put("visibilityExternalJitStateBefore", externalJit); save(report);
        modifierCheck(report, "visibility_external_jit_warm", externalJit == 2);
        before = commitCalls();
        int narrow = redefine(identity, asset("modifier-narrow/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "visibility_narrowing_commit", narrow == 0 && commitCalls() == before + 1
                && Modifier.isProtected(identity.getDeclaredField("value").getModifiers())
                && Modifier.isProtected(identity.getDeclaredMethod("compute").getModifiers())
                && (identity.getDeclaredField("packageValue").getModifiers()
                    & (Modifier.PUBLIC | Modifier.PROTECTED | Modifier.PRIVATE)) == 0
                && (identity.getDeclaredMethod("packageCompute").getModifiers()
                    & (Modifier.PUBLIC | Modifier.PROTECTED | Modifier.PRIVATE)) == 0
                && Modifier.isProtected(cachedField.getModifiers())
                && Modifier.isProtected(cachedMethod.getModifiers())
                && instance.throughPublic() == 76 && cachedField.getInt(instance) == 73
                && (Integer) cachedMethod.invoke(instance) == 76);
        String fieldOutcome = modifierAccessOutcome(externalField, instance);
        String methodOutcome = modifierAccessOutcome(externalMethod, instance);
        String packageFieldOutcome = modifierAccessOutcome(externalPackageField, instance);
        String packageMethodOutcome = modifierAccessOutcome(externalPackageMethod, instance);
        report.put("visibilityExternalOutcomes", new JSONObject()
                .put("field", fieldOutcome).put("method", methodOutcome)
                .put("packageField", packageFieldOutcome).put("packageMethod", packageMethodOutcome)); save(report);
        JSONObject freshOutcomes = new JSONObject();
        Method[] freshExternalMethods = new Method[4];
        try {
            ClassLoader freshExternalLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                    "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalCaller.dex")), identity.getClassLoader());
            Class<?> freshExternal = Class.forName("com.github.ytlog.artdcevm.external.ModifierExternalCaller", true,
                    freshExternalLoader);
            String[] accessNames = {"field", "method", "packageField", "packageMethod"};
            for (int i = 0; i < accessNames.length; ++i) {
                freshExternalMethods[i] = freshExternal.getMethod(accessNames[i], ModifierSubject.class);
                freshOutcomes.put(accessNames[i], modifierAccessOutcome(freshExternalMethods[i], instance));
            }
            report.put("visibilityFreshExternalOutcomes", freshOutcomes); save(report);
        } catch (LinkageError error) {
            report.put("visibilityFreshExternalLinkageError", error.toString()); save(report);
        }
        modifierCheck(report, "visibility_resolved_external_link_coherent",
                (fieldOutcome.equals("returned:73") || fieldOutcome.equals("java.lang.IllegalAccessError"))
                && (methodOutcome.equals("returned:76") || methodOutcome.equals("java.lang.IllegalAccessError"))
                && (packageFieldOutcome.equals("returned:23") || packageFieldOutcome.equals("java.lang.IllegalAccessError"))
                && (packageMethodOutcome.equals("returned:32") || packageMethodOutcome.equals("java.lang.IllegalAccessError")));
        modifierCheck(report, "visibility_fresh_external_denied",
                freshOutcomes.length() == 4
                && freshOutcomes.getString("field").equals("java.lang.IllegalAccessError")
                && freshOutcomes.getString("method").equals("java.lang.IllegalAccessError")
                && freshOutcomes.getString("packageField").equals("java.lang.IllegalAccessError")
                && freshOutcomes.getString("packageMethod").equals("java.lang.IllegalAccessError"));
        boolean freshHandleDenied = false;
        try { MethodHandles.publicLookup().findVirtual(identity, "compute", MethodType.methodType(int.class)); }
        catch (IllegalAccessException expected) { freshHandleDenied = true; }
        modifierCheck(report, "visibility_fresh_public_handle_denied", freshHandleDenied);
        before = commitCalls();
        int publicAgain = redefine(identity, asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        String restoredExternalMethod = modifierAccessOutcome(externalMethod, instance);
        report.put("visibilityRestoredExternalMethod", restoredExternalMethod); save(report);
        modifierCheck(report, "visibility_public_restore", publicAgain == 0 && commitCalls() == before + 1
                && Modifier.isPublic(cachedField.getModifiers())
                && Modifier.isPublic(cachedMethod.getModifiers())
                && instance.throughPublic() == 75
                && restoredExternalMethod.equals("returned:75"));
        modifierCheck(report, "visibility_denied_link_recovers_after_restore",
                freshExternalMethods[1] != null
                && modifierAccessOutcome(freshExternalMethods[1], instance).equals("returned:75"));
        before = commitCalls();
        int protectedSync = redefine(identity,
                asset("modifier-protected-sync/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("visibilitySyncCode", protectedSync)
                .put("visibilitySyncCommitDelta", commitCalls() - before); save(report);
        modifierCheck(report, "visibility_synchronized_combined_commit",
                protectedSync == 0 && commitCalls() == before + 1
                && Modifier.isProtected(cachedMethod.getModifiers())
                && Modifier.isSynchronized(cachedMethod.getModifiers())
                && Modifier.isProtected(identity.getDeclaredMethod("compute").getModifiers())
                && Modifier.isSynchronized(identity.getDeclaredMethod("compute").getModifiers())
                && instance.throughPublic() == 75);
        modifierCheck(report, "visibility_synchronized_combined_monitor",
                modifierMonitorCheck(instance, () -> instance.throughPublic(), true, 75)
                && modifierMonitorCheck(instance,
                        () -> (Integer) cachedMethod.invoke(instance), true, 75)
                && modifierMonitorCheck(instance,
                        () -> (int) freshHandle.invokeWithArguments(instance), true, 75));
        ClassLoader combinedExternalLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalCaller.dex")), identity.getClassLoader());
        Method combinedExternalMethod = Class.forName(
                "com.github.ytlog.artdcevm.external.ModifierExternalCaller", true, combinedExternalLoader)
                .getMethod("method", ModifierSubject.class);
        modifierCheck(report, "visibility_synchronized_fresh_external_denied",
                modifierAccessOutcome(combinedExternalMethod, instance)
                        .equals("java.lang.IllegalAccessError"));
        before = commitCalls();
        int clearProtectedSync = redefine(identity,
                asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "visibility_synchronized_combined_clear",
                clearProtectedSync == 0 && commitCalls() == before + 1
                && Modifier.isPublic(cachedMethod.getModifiers())
                && !Modifier.isSynchronized(cachedMethod.getModifiers())
                && modifierMonitorCheck(instance, () -> instance.throughPublic(), false, 75)
                && modifierAccessOutcome(combinedExternalMethod, instance).equals("returned:75"));
        VarHandle combinedFieldHandle = MethodHandles.publicLookup().findVarHandle(
                identity, "value", int.class);
        before = commitCalls();
        int protectedVolatile = redefine(identity,
                asset("modifier-protected-volatile/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("visibilityVolatileCode", protectedVolatile)
                .put("visibilityVolatileCommitDelta", commitCalls() - before); save(report);
        modifierCheck(report, "visibility_volatile_combined_commit",
                protectedVolatile == 0 && commitCalls() == before + 1
                && Modifier.isProtected(cachedField.getModifiers())
                && Modifier.isVolatile(cachedField.getModifiers())
                && Modifier.isProtected(identity.getDeclaredField("value").getModifiers())
                && Modifier.isVolatile(identity.getDeclaredField("value").getModifiers())
                && cachedField.getInt(instance) == 73
                && (int) combinedFieldHandle.getVolatile(instance) == 73);
        combinedFieldHandle.setVolatile(instance, 74);
        modifierCheck(report, "visibility_volatile_cached_handle_write",
                cachedField.getInt(instance) == 74 && instance.throughPublic() == 76);
        combinedFieldHandle.setVolatile(instance, 73);
        ClassLoader combinedFieldExternalLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalCaller.dex")), identity.getClassLoader());
        Method combinedExternalField = Class.forName(
                "com.github.ytlog.artdcevm.external.ModifierExternalCaller", true, combinedFieldExternalLoader)
                .getMethod("field", ModifierSubject.class);
        modifierCheck(report, "visibility_volatile_fresh_external_denied",
                modifierAccessOutcome(combinedExternalField, instance)
                        .equals("java.lang.IllegalAccessError"));
        before = commitCalls();
        int clearProtectedVolatile = redefine(identity,
                asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "visibility_volatile_combined_clear",
                clearProtectedVolatile == 0 && commitCalls() == before + 1
                && Modifier.isPublic(cachedField.getModifiers())
                && !Modifier.isVolatile(cachedField.getModifiers())
                && (int) combinedFieldHandle.get(instance) == 73
                && modifierAccessOutcome(combinedExternalField, instance).equals("returned:73"));
        before = commitCalls();
        int protectedFinal = redefine(identity,
                asset("modifier-protected-final/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("visibilityFinalCode", protectedFinal)
                .put("visibilityFinalCommitDelta", commitCalls() - before); save(report);
        modifierCheck(report, "visibility_final_combined_commit",
                protectedFinal == 0 && commitCalls() == before + 1
                && Modifier.isProtected(cachedField.getModifiers())
                && Modifier.isFinal(cachedField.getModifiers())
                && Modifier.isProtected(identity.getDeclaredField("value").getModifiers())
                && Modifier.isFinal(identity.getDeclaredField("value").getModifiers())
                && cachedField.getInt(instance) == 73
                && (int) combinedFieldHandle.get(instance) == 73);
        boolean combinedOldVarHandleDenied = false;
        try { combinedFieldHandle.set(instance, 99); }
        catch (UnsupportedOperationException expected) { combinedOldVarHandleDenied = true; }
        modifierCheck(report, "visibility_final_old_varhandle_write_denied",
                combinedOldVarHandleDenied && cachedField.getInt(instance) == 73);
        ClassLoader combinedFinalExternalLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalCaller.dex")), identity.getClassLoader());
        Method combinedFinalExternalField = Class.forName(
                "com.github.ytlog.artdcevm.external.ModifierExternalCaller", true, combinedFinalExternalLoader)
                .getMethod("field", ModifierSubject.class);
        modifierCheck(report, "visibility_final_fresh_external_denied",
                modifierAccessOutcome(combinedFinalExternalField, instance)
                        .equals("java.lang.IllegalAccessError"));
        before = commitCalls();
        int clearProtectedFinal = redefine(identity,
                asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        combinedFieldHandle.set(instance, 75);
        modifierCheck(report, "visibility_final_combined_clear",
                clearProtectedFinal == 0 && commitCalls() == before + 1
                && Modifier.isPublic(cachedField.getModifiers())
                && !Modifier.isFinal(cachedField.getModifiers())
                && cachedField.getInt(instance) == 75
                && modifierAccessOutcome(combinedFinalExternalField, instance).equals("returned:75"));
        combinedFieldHandle.set(instance, 73);
        before = commitCalls();
        int synchronizedCode = redefine(identity, asset("modifier-sync/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "synchronized_commit", synchronizedCode == 0 && commitCalls() == before + 1
                && Modifier.isSynchronized(cachedMethod.getModifiers())
                && Modifier.isSynchronized(identity.getMethod("compute").getModifiers())
                && instance.throughPublic() == 75);
        modifierCheck(report, "synchronized_monitor",
                modifierMonitorCheck(instance, () -> instance.throughPublic(), true, 75));
        modifierCheck(report, "synchronized_cached_reflection_monitor",
                modifierMonitorCheck(instance, () -> (Integer) cachedMethod.invoke(instance), true, 75));
        modifierCheck(report, "synchronized_cached_handle_monitor",
                modifierMonitorCheck(instance, () -> (int) freshHandle.invokeWithArguments(instance), true, 75));
        before = commitCalls();
        int unsynchronizedCode = redefine(identity, asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "unsynchronized_commit", unsynchronizedCode == 0 && commitCalls() == before + 1
                && !Modifier.isSynchronized(cachedMethod.getModifiers())
                && !Modifier.isSynchronized(identity.getMethod("compute").getModifiers())
                && instance.throughPublic() == 75);
        modifierCheck(report, "unsynchronized_monitor",
                modifierMonitorCheck(instance, () -> instance.throughPublic(), false, 75));
        modifierCheck(report, "unsynchronized_cached_reflection_monitor",
                modifierMonitorCheck(instance, () -> (Integer) cachedMethod.invoke(instance), false, 75));
        modifierCheck(report, "unsynchronized_cached_handle_monitor",
                modifierMonitorCheck(instance, () -> (int) freshHandle.invokeWithArguments(instance), false, 75));
        before = commitCalls();
        int synchronizedStaticCode = redefine(identity,
                asset("modifier-sync-static/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "static_synchronized_commit", synchronizedStaticCode == 0
                && commitCalls() == before + 1
                && Modifier.isSynchronized(cachedStaticMethod.getModifiers())
                && Modifier.isSynchronized(identity.getMethod("staticCompute").getModifiers())
                && ModifierSubject.staticCompute() == 19);
        modifierCheck(report, "static_synchronized_monitor",
                modifierMonitorCheck(identity, () -> ModifierSubject.staticCompute(), true, 19));
        modifierCheck(report, "static_synchronized_cached_reflection_monitor",
                modifierMonitorCheck(identity, () -> (Integer) cachedStaticMethod.invoke(null), true, 19));
        modifierCheck(report, "static_synchronized_cached_handle_monitor",
                modifierMonitorCheck(identity, () -> (int) staticHandle.invokeWithArguments(), true, 19));
        before = commitCalls();
        int unsynchronizedStaticCode = redefine(identity,
                asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "static_unsynchronized_commit", unsynchronizedStaticCode == 0
                && commitCalls() == before + 1
                && !Modifier.isSynchronized(cachedStaticMethod.getModifiers())
                && !Modifier.isSynchronized(identity.getMethod("staticCompute").getModifiers())
                && ModifierSubject.staticCompute() == 19);
        modifierCheck(report, "static_unsynchronized_monitor",
                modifierMonitorCheck(identity, () -> ModifierSubject.staticCompute(), false, 19));
        modifierCheck(report, "static_unsynchronized_cached_reflection_monitor",
                modifierMonitorCheck(identity, () -> (Integer) cachedStaticMethod.invoke(null), false, 19));
        modifierCheck(report, "static_unsynchronized_cached_handle_monitor",
                modifierMonitorCheck(identity, () -> (int) staticHandle.invokeWithArguments(), false, 19));
        before = commitCalls();
        int finalSyncCode = redefine(identity,
                asset("modifier-final-sync/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("combinedFinalSyncCode", finalSyncCode)
                .put("combinedFinalSyncCommitDelta", commitCalls() - before)
                .put("combinedFinalSyncCachedFlags", cachedFinalMethod.getModifiers())
                .put("combinedFinalSyncFreshFlags", identity.getMethod("finalizable").getModifiers())
                .put("combinedFinalSyncDirectValue", instance.finalizable())
                .put("combinedFinalSyncPassiveValue", passive.finalizable()); save(report);
        modifierCheck(report, "combined_final_synchronized_commit",
                finalSyncCode == 0 && commitCalls() == before + 1
                && Modifier.isFinal(cachedFinalMethod.getModifiers())
                && Modifier.isSynchronized(cachedFinalMethod.getModifiers())
                && Modifier.isFinal(identity.getMethod("finalizable").getModifiers())
                && Modifier.isSynchronized(identity.getMethod("finalizable").getModifiers())
                && instance.finalizable() == 30 && passive.finalizable() == 30);
        modifierCheck(report, "combined_final_synchronized_monitor",
                modifierMonitorCheck(instance, () -> instance.finalizable(), true, 30)
                && modifierMonitorCheck(instance,
                        () -> (Integer) cachedFinalMethod.invoke(instance), true, 30)
                && modifierMonitorCheck(instance,
                        () -> (int) finalHandle.invokeWithArguments(instance), true, 30));
        before = commitCalls();
        int clearFinalSync = redefine(identity,
                asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "combined_final_synchronized_clear",
                clearFinalSync == 0 && commitCalls() == before + 1
                && !Modifier.isFinal(cachedFinalMethod.getModifiers())
                && !Modifier.isSynchronized(cachedFinalMethod.getModifiers())
                && modifierMonitorCheck(instance, () -> instance.finalizable(), false, 29)
                && modifierMonitorCheck(instance,
                        () -> (Integer) cachedFinalMethod.invoke(instance), false, 29)
                && modifierMonitorCheck(instance,
                        () -> (int) finalHandle.invokeWithArguments(instance), false, 29));
        Method hotCaller = ModifierHotCaller.class.getMethod("call", ModifierSubject.class);
        for (int i = 0; i < 100000; ++i) {
            sink ^= ModifierHotCaller.call((i & 1) == 0 ? instance : passive);
        }
        int hotCallerJitState = jitMethodState(hotCaller);
        report.put("finalHotCallerJitStateBefore", hotCallerJitState); save(report);
        modifierCheck(report, "final_hot_caller_warm", hotCallerJitState >= 0
                && ModifierHotCaller.call(instance) == 29);
        before = commitCalls();
        int finalCode = redefine(identity, asset("modifier-final/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "final_method_commit", finalCode == 0 && commitCalls() == before + 1
                && Modifier.isFinal(cachedFinalMethod.getModifiers())
                && Modifier.isFinal(identity.getMethod("finalizable").getModifiers())
                && instance.finalizable() == 30
                && passive.finalizable() == 30
                && ModifierHotCaller.call(instance) == 30
                && (Integer) cachedFinalMethod.invoke(instance) == 30);
        try {
            modifierCheck(report, "final_method_handle", (int) finalHandle.invokeWithArguments(instance) == 30);
        } catch (Throwable error) { throw new AssertionError("Final method handle", error); }
        boolean overrideRejected = false;
        try {
            ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                    "modifier-override/com/github/ytlog/artdcevm/fixture/ModifierOverride.dex")), identity.getClassLoader());
            Class.forName("com.github.ytlog.artdcevm.fixture.ModifierOverride", true, loader)
                    .getDeclaredConstructor().newInstance();
        } catch (LinkageError expected) { overrideRejected = true; }
        modifierCheck(report, "final_override_rejected", overrideRejected);
        before = commitCalls();
        int nonFinalCode = redefine(identity, asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        modifierCheck(report, "nonfinal_method_commit", nonFinalCode == 0 && commitCalls() == before + 1
                && !Modifier.isFinal(cachedFinalMethod.getModifiers())
                && !Modifier.isFinal(identity.getMethod("finalizable").getModifiers())
                && instance.finalizable() == 29 && passive.finalizable() == 29
                && ModifierHotCaller.call(instance) == 29);
        ClassLoader overrideLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-override/com/github/ytlog/artdcevm/fixture/ModifierOverride.dex")), identity.getClassLoader());
        ModifierSubject override = (ModifierSubject) Class.forName(
                "com.github.ytlog.artdcevm.fixture.ModifierOverride", true, overrideLoader)
                .getDeclaredConstructor().newInstance();
        modifierCheck(report, "nonfinal_override_dispatch", override.finalizable() == 31
                && ModifierHotCaller.call(override) == 31
                && (Integer) cachedFinalMethod.invoke(override) == 31);
        before = commitCalls();
        int combinedWithOverride = redefine(identity,
                asset("modifier-final-sync/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        report.put("combinedFinalSyncLoadedOverrideCode", combinedWithOverride)
                .put("combinedFinalSyncLoadedOverrideCommitDelta", commitCalls() - before); save(report);
        modifierCheck(report, "combined_final_synchronized_loaded_override_rejected",
                combinedWithOverride == 71 && commitCalls() == before
                && !Modifier.isFinal(cachedFinalMethod.getModifiers())
                && !Modifier.isSynchronized(cachedFinalMethod.getModifiers())
                && override.finalizable() == 31 && instance.finalizable() == 29);
        before = commitCalls();
        PatchResult loadedOverrideResult = backend.apply(new PatchRequest(
                PatchRequest.Operation.EVOLVE_SCHEMA, new Class<?>[]{identity},
                new byte[][]{asset("modifier-final/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex")}));
        int finalWithLoadedOverride = loadedOverrideResult.code;
        report.put("finalWithLoadedOverrideResult", finalWithLoadedOverride)
                .put("finalWithLoadedOverrideDetail", loadedOverrideResult.detail); save(report);
        boolean oldOverrideCoherent = override.finalizable() == 31
                && (Integer) cachedFinalMethod.invoke(override) == 31;
        modifierCheck(report, "final_loaded_override_coherent", oldOverrideCoherent
                && (finalWithLoadedOverride == 0
                    ? commitCalls() == before + 1 && Modifier.isFinal(cachedFinalMethod.getModifiers())
                    : finalWithLoadedOverride == 71 && commitCalls() == before
                      && !Modifier.isFinal(cachedFinalMethod.getModifiers())));
        if (finalWithLoadedOverride == 0) {
            before = commitCalls();
            int restore = redefine(identity, asset("modifier-public/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
            modifierCheck(report, "final_loaded_override_restore", restore == 0
                    && commitCalls() == before + 1 && override.finalizable() == 31
                    && !Modifier.isFinal(cachedFinalMethod.getModifiers()));
        }
        ClassLoader externalOverrideLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "modifier-external/com/github/ytlog/artdcevm/external/ModifierExternalOverride.dex")), identity.getClassLoader());
        ModifierSubject externalOverride = (ModifierSubject) Class.forName(
                "com.github.ytlog.artdcevm.external.ModifierExternalOverride", true, externalOverrideLoader)
                .getDeclaredConstructor().newInstance();
        Method externalOverrideCompute = externalOverride.getClass().getMethod("compute");
        Method externalOverridePackage = externalOverride.getClass().getMethod("packageCompute");
        modifierCheck(report, "visibility_external_override_baseline",
                (Integer) externalOverrideCompute.invoke(externalOverride) == 99
                && (Integer) externalOverridePackage.invoke(externalOverride) == 98);
        before = commitCalls();
        int narrowWithOverride = redefine(identity, asset("modifier-narrow/com/github/ytlog/artdcevm/fixture/ModifierSubject.dex"));
        int externalComputeAfter = (Integer) externalOverrideCompute.invoke(externalOverride);
        int externalPackageAfter = (Integer) externalOverridePackage.invoke(externalOverride);
        int parentPackageOnExternal = (Integer) cachedPackageMethod.invoke(externalOverride);
        int parentComputeOnExternal = (Integer) cachedMethod.invoke(externalOverride);
        report.put("visibilityNarrowWithLoadedOverrideResult", narrowWithOverride)
                .put("visibilityExternalComputeAfter", externalComputeAfter)
                .put("visibilityExternalPackageAfter", externalPackageAfter)
                .put("visibilityParentPackageOnExternal", parentPackageOnExternal)
                .put("visibilityParentComputeOnExternal", parentComputeOnExternal); save(report);
        modifierCheck(report, "visibility_loaded_override_relinked", narrowWithOverride == 0
                && commitCalls() == before + 1 && externalComputeAfter == 99
                && externalPackageAfter == 98 && parentPackageOnExternal == 14
                && parentComputeOnExternal == 99
                && Modifier.isProtected(cachedMethod.getModifiers())
                && (cachedPackageMethod.getModifiers() & (Modifier.PUBLIC | Modifier.PROTECTED
                        | Modifier.PRIVATE)) == 0);
        report.put("visibilityWideningPassed", true).put("synchronizedTransitionPassed", true)
                .put("finalMethodTransitionPassed", true)
                .put("phase", "finished"); save(report);
        return true;
    }
    private boolean testVolatileFieldTransition(JSONObject report) throws Exception {
        if (!report.has("modifierChecks")) report.put("modifierChecks", new JSONArray());
        report.put("phase", "volatile_field_baseline"); save(report);
        modifierCheck(report, "volatile_field_capability",
                backend.capabilities().getBoolean("fieldVolatileTransition"));
        FinalFieldSubject subject = new FinalFieldSubject();
        FinalFieldCaller.write(subject, 73);
        FinalFieldCaller.writeStatic(84);
        Field cached = FinalFieldSubject.class.getField("value");
        Field cachedStatic = FinalFieldSubject.class.getField("staticValue");
        VarHandle instanceHandle = MethodHandles.lookup().findVarHandle(
                FinalFieldSubject.class, "value", int.class);
        VarHandle staticHandle = MethodHandles.lookup().findStaticVarHandle(
                FinalFieldSubject.class, "staticValue", int.class);
        Method hot = FinalFieldCaller.class.getMethod("read", FinalFieldSubject.class);
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= FinalFieldCaller.read(subject);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("volatileFieldReaderJitStateBefore", jit); save(report);
        modifierCheck(report, "volatile_field_baseline", jit == 2
                && !Modifier.isVolatile(cached.getModifiers())
                && !Modifier.isVolatile(cachedStatic.getModifiers())
                && FinalFieldCaller.read(subject) == 73
                && FinalFieldCaller.readStatic() == 84);
        long before = commitCalls();
        int added = redefine(FinalFieldSubject.class, asset(
                "modifier-field-volatile/com/github/ytlog/artdcevm/fixture/FinalFieldSubject.dex"));
        report.put("volatileFieldAddResult", added); save(report);
        modifierCheck(report, "volatile_field_add_commit", added == 0
                && commitCalls() == before + 1
                && Modifier.isVolatile(cached.getModifiers())
                && Modifier.isVolatile(cachedStatic.getModifiers())
                && Modifier.isVolatile(FinalFieldSubject.class.getField("value").getModifiers())
                && Modifier.isVolatile(FinalFieldSubject.class.getField("staticValue").getModifiers())
                && FinalFieldCaller.read(subject) == 73
                && FinalFieldCaller.readStatic() == 84
                && (int) instanceHandle.getVolatile(subject) == 73
                && (int) staticHandle.getVolatile() == 84);
        instanceHandle.setVolatile(subject, 91);
        staticHandle.setVolatile(92);
        modifierCheck(report, "volatile_field_old_handles_write", cached.getInt(subject) == 91
                && cachedStatic.getInt(null) == 92
                && FinalFieldCaller.read(subject) == 91
                && FinalFieldCaller.readStatic() == 92);
        before = commitCalls();
        int removed = redefine(FinalFieldSubject.class, asset(
                "modifier-field-original/com/github/ytlog/artdcevm/fixture/FinalFieldSubject.dex"));
        report.put("volatileFieldRemoveResult", removed); save(report);
        modifierCheck(report, "volatile_field_remove_commit", removed == 0
                && commitCalls() == before + 1
                && !Modifier.isVolatile(cached.getModifiers())
                && !Modifier.isVolatile(cachedStatic.getModifiers())
                && FinalFieldCaller.read(subject) == 91
                && FinalFieldCaller.readStatic() == 92
                && (int) instanceHandle.getVolatile(subject) == 91
                && (int) staticHandle.getVolatile() == 92);
        report.put("volatileFieldTransitionPassed", true); save(report);
        return true;
    }
    private boolean testFinalFieldTransition(JSONObject report) throws Exception {
        report.put("phase", "final_field_baseline"); save(report);
        modifierCheck(report, "final_field_capability",
                backend.capabilities().getBoolean("fieldFinalTransition"));
        FinalFieldSubject subject = new FinalFieldSubject();
        FinalFieldCaller.write(subject, 73);
        FinalFieldCaller.writeStatic(84);
        Field cached = FinalFieldSubject.class.getField("value");
        Field cachedStatic = FinalFieldSubject.class.getField("staticValue");
        VarHandle oldInstanceVarHandle = MethodHandles.lookup().findVarHandle(
                FinalFieldSubject.class, "value", int.class);
        VarHandle oldStaticVarHandle = MethodHandles.lookup().findStaticVarHandle(
                FinalFieldSubject.class, "staticValue", int.class);
        MethodHandle oldInstanceSetter = MethodHandles.lookup().findSetter(
                FinalFieldSubject.class, "value", int.class);
        MethodHandle oldStaticSetter = MethodHandles.lookup().findStaticSetter(
                FinalFieldSubject.class, "staticValue", int.class);
        FinalFieldHandleCaller.initialize(oldInstanceSetter, oldStaticSetter);
        Method compiledInstanceSetter = FinalFieldHandleCaller.class.getMethod(
                "writeInstance", FinalFieldSubject.class, int.class);
        Method compiledStaticSetter = FinalFieldHandleCaller.class.getMethod("writeStatic", int.class);
        int instanceSetterJit = 0, staticSetterJit = 0;
        try {
            for (int attempt = 0; attempt < 12 && (instanceSetterJit != 2 || staticSetterJit != 2);
                    ++attempt) {
                for (int i = 0; i < 50000; ++i) {
                    FinalFieldHandleCaller.writeInstance(subject, 73);
                    FinalFieldHandleCaller.writeStatic(84);
                }
                Thread.sleep(100);
                instanceSetterJit = jitMethodState(compiledInstanceSetter);
                staticSetterJit = jitMethodState(compiledStaticSetter);
            }
        } catch (Throwable error) { throw new AssertionError("Warm field handle setters", error); }
        report.put("finalFieldHandleInstanceCallerJitBefore", instanceSetterJit)
                .put("finalFieldHandleStaticCallerJitBefore", staticSetterJit); save(report);
        modifierCheck(report, "final_field_old_handle_callers_compiled",
                instanceSetterJit == 2 && staticSetterJit == 2);
        Method hot = FinalFieldCaller.class.getMethod("read", FinalFieldSubject.class);
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= FinalFieldCaller.read(subject);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("finalFieldReaderJitStateBefore", jit); save(report);
        modifierCheck(report, "final_field_baseline", jit == 2 && !Modifier.isFinal(cached.getModifiers())
                && !Modifier.isFinal(cachedStatic.getModifiers())
                && cached.getInt(subject) == 73 && FinalFieldCaller.read(subject) == 73
                && cachedStatic.getInt(null) == 84 && FinalFieldCaller.readStatic() == 84);
        long before = commitCalls();
        int added = redefine(FinalFieldSubject.class, asset(
                "modifier-field-final/com/github/ytlog/artdcevm/fixture/FinalFieldSubject.dex"));
        report.put("finalFieldAddResult", added).put("finalFieldAfterAdd", new JSONObject()
                .put("commitCalls", commitCalls()).put("cachedModifiers", cached.getModifiers())
                .put("freshModifiers", FinalFieldSubject.class.getField("value").getModifiers())
                .put("cachedStaticModifiers", cachedStatic.getModifiers())
                .put("cachedValue", cached.getInt(subject))
                .put("cachedStaticValue", cachedStatic.getInt(null))
                .put("hotValue", FinalFieldCaller.read(subject))
                .put("hotStaticValue", FinalFieldCaller.readStatic())
                .put("ownRead", subject.read())
                .put("ownStaticRead", FinalFieldSubject.readStatic())); save(report);
        modifierCheck(report, "final_field_add_commit", added == 0 && commitCalls() == before + 1
                && Modifier.isFinal(cached.getModifiers())
                && Modifier.isFinal(cachedStatic.getModifiers())
                && Modifier.isFinal(FinalFieldSubject.class.getField("value").getModifiers())
                && Modifier.isFinal(FinalFieldSubject.class.getField("staticValue").getModifiers())
                && cached.getInt(subject) == 73 && FinalFieldCaller.read(subject) == 73
                && cachedStatic.getInt(null) == 84 && FinalFieldCaller.readStatic() == 84
                && subject.read() == 41 && FinalFieldSubject.readStatic() == 52);
        String oldInstanceVarHandleOutcome = "returned";
        String oldStaticVarHandleOutcome = "returned";
        try { oldInstanceVarHandle.set(subject, 99); }
        catch (Throwable error) { oldInstanceVarHandleOutcome = error.getClass().getName(); }
        try { oldStaticVarHandle.set(99); }
        catch (Throwable error) { oldStaticVarHandleOutcome = error.getClass().getName(); }
        report.put("finalFieldOldInstanceVarHandleOutcome", oldInstanceVarHandleOutcome)
                .put("finalFieldOldStaticVarHandleOutcome", oldStaticVarHandleOutcome)
                .put("finalFieldAfterOldVarHandle", cached.getInt(subject))
                .put("finalStaticAfterOldVarHandle", cachedStatic.getInt(null)); save(report);
        modifierCheck(report, "final_field_old_varhandle_writers_denied",
                oldInstanceVarHandleOutcome.equals("java.lang.UnsupportedOperationException")
                && oldStaticVarHandleOutcome.equals("java.lang.UnsupportedOperationException")
                && cached.getInt(subject) == 73 && cachedStatic.getInt(null) == 84);
        boolean oldInstanceCasDenied = false, oldStaticCasDenied = false;
        boolean oldInstanceUpdateDenied = false, oldStaticUpdateDenied = false;
        try { oldInstanceVarHandle.compareAndSet(subject, 73, 99); }
        catch (UnsupportedOperationException expected) { oldInstanceCasDenied = true; }
        try { oldStaticVarHandle.compareAndSet(84, 99); }
        catch (UnsupportedOperationException expected) { oldStaticCasDenied = true; }
        try { oldInstanceVarHandle.getAndAdd(subject, 1); }
        catch (UnsupportedOperationException expected) { oldInstanceUpdateDenied = true; }
        try { oldStaticVarHandle.getAndAdd(1); }
        catch (UnsupportedOperationException expected) { oldStaticUpdateDenied = true; }
        modifierCheck(report, "final_field_old_varhandle_atomic_writers_denied",
                oldInstanceCasDenied && oldStaticCasDenied
                && oldInstanceUpdateDenied && oldStaticUpdateDenied
                && cached.getInt(subject) == 73 && cachedStatic.getInt(null) == 84);
        String oldInstanceSetterOutcome = "returned", oldStaticSetterOutcome = "returned";
        try { oldInstanceSetter.invokeWithArguments(subject, 99); }
        catch (Throwable error) { oldInstanceSetterOutcome = error.getClass().getName(); }
        try { oldStaticSetter.invokeWithArguments(99); }
        catch (Throwable error) { oldStaticSetterOutcome = error.getClass().getName(); }
        report.put("finalFieldOldInstanceSetterOutcome", oldInstanceSetterOutcome)
                .put("finalFieldOldStaticSetterOutcome", oldStaticSetterOutcome)
                .put("finalFieldAfterOldSetter", cached.getInt(subject))
                .put("finalStaticAfterOldSetter", cachedStatic.getInt(null)); save(report);
        modifierCheck(report, "final_field_old_method_handle_setters_denied",
                oldInstanceSetterOutcome.equals("java.lang.IllegalAccessError")
                && oldStaticSetterOutcome.equals("java.lang.IllegalAccessError")
                && cached.getInt(subject) == 73 && cachedStatic.getInt(null) == 84);
        String compiledInstanceOutcome = "returned", compiledStaticOutcome = "returned";
        try { FinalFieldHandleCaller.writeInstance(subject, 99); }
        catch (Throwable error) { compiledInstanceOutcome = error.getClass().getName(); }
        try { FinalFieldHandleCaller.writeStatic(99); }
        catch (Throwable error) { compiledStaticOutcome = error.getClass().getName(); }
        report.put("finalFieldCompiledInstanceSetterOutcome", compiledInstanceOutcome)
                .put("finalFieldCompiledStaticSetterOutcome", compiledStaticOutcome)
                .put("finalFieldAfterCompiledSetter", cached.getInt(subject))
                .put("finalStaticAfterCompiledSetter", cachedStatic.getInt(null)); save(report);
        modifierCheck(report, "final_field_compiled_old_handle_setters_denied",
                compiledInstanceOutcome.equals("java.lang.IllegalAccessError")
                && compiledStaticOutcome.equals("java.lang.IllegalAccessError")
                && cached.getInt(subject) == 73 && cachedStatic.getInt(null) == 84);
        boolean oldWriterDenied = false;
        try { FinalFieldCaller.write(subject, 99); }
        catch (IllegalAccessError expected) { oldWriterDenied = true; }
        modifierCheck(report, "final_field_old_writer_denied", oldWriterDenied
                && FinalFieldCaller.read(subject) == 73);
        boolean oldStaticWriterDenied = false;
        try { FinalFieldCaller.writeStatic(99); }
        catch (IllegalAccessError expected) { oldStaticWriterDenied = true; }
        modifierCheck(report, "final_static_field_old_writer_denied", oldStaticWriterDenied
                && FinalFieldCaller.readStatic() == 84);
        boolean freshSetterDenied = false;
        try { MethodHandles.publicLookup().findSetter(FinalFieldSubject.class, "value", int.class); }
        catch (IllegalAccessException expected) { freshSetterDenied = true; }
        modifierCheck(report, "final_field_fresh_setter_denied", freshSetterDenied);
        boolean freshStaticSetterDenied = false;
        try { MethodHandles.publicLookup().findStaticSetter(FinalFieldSubject.class, "staticValue", int.class); }
        catch (IllegalAccessException expected) { freshStaticSetterDenied = true; }
        modifierCheck(report, "final_static_field_fresh_setter_denied", freshStaticSetterDenied);
        before = commitCalls();
        int removed = redefine(FinalFieldSubject.class, asset(
                "modifier-field-original/com/github/ytlog/artdcevm/fixture/FinalFieldSubject.dex"));
        report.put("finalFieldRemoveResult", removed); save(report);
        try {
            oldInstanceSetter.invokeWithArguments(subject, 93);
            oldStaticSetter.invokeWithArguments(94);
            FinalFieldHandleCaller.writeInstance(subject, 95);
            FinalFieldHandleCaller.writeStatic(96);
        } catch (Throwable error) { throw new AssertionError("Restored field handle setters", error); }
        modifierCheck(report, "final_field_old_handle_setters_restored",
                cached.getInt(subject) == 95 && cachedStatic.getInt(null) == 96);
        FinalFieldCaller.write(subject, 91);
        FinalFieldCaller.writeStatic(92);
        oldInstanceVarHandle.set(subject, 91);
        oldStaticVarHandle.set(92);
        modifierCheck(report, "final_field_remove_commit", removed == 0 && commitCalls() == before + 1
                && !Modifier.isFinal(cached.getModifiers()) && !Modifier.isFinal(cachedStatic.getModifiers())
                && FinalFieldCaller.read(subject) == 91 && FinalFieldCaller.readStatic() == 92
                && (int) oldInstanceVarHandle.get(subject) == 91
                && (int) oldStaticVarHandle.get() == 92
                && subject.read() == 91 && FinalFieldSubject.readStatic() == 92);
        report.put("finalFieldTransitionPassed", true); save(report);
        return true;
    }
    private boolean testClassFinalTransition(JSONObject report) throws Exception {
        report.put("phase", "class_final_baseline"); save(report);
        modifierCheck(report, "class_final_capability",
                backend.capabilities().getBoolean("classFinalTransition"));
        ClassFinalSubject subject = new ClassFinalSubject();
        Method hot = ClassFinalHotCaller.class.getMethod("call", ClassFinalSubject.class);
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= ClassFinalHotCaller.call(subject);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("classFinalCallerJitStateBefore", jit); save(report);
        modifierCheck(report, "class_final_baseline", jit == 2
                && !Modifier.isFinal(ClassFinalSubject.class.getModifiers())
                && ClassFinalHotCaller.call(subject) == 29);
        long before = commitCalls();
        int added = redefine(ClassFinalSubject.class, asset(
                "class-final/com/github/ytlog/artdcevm/fixture/ClassFinalSubject.dex"));
        report.put("classFinalAddResult", added); save(report);
        modifierCheck(report, "class_final_add_commit", added == 0 && commitCalls() == before + 1
                && Modifier.isFinal(ClassFinalSubject.class.getModifiers())
                && subject.getClass() == ClassFinalSubject.class && subject.value() == 30
                && ClassFinalHotCaller.call(subject) == 30);
        boolean newSubclassRejected = false;
        try {
            ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                    "class-final-child/com/github/ytlog/artdcevm/fixture/ClassFinalChild.dex")),
                    ClassFinalSubject.class.getClassLoader());
            Class.forName("com.github.ytlog.artdcevm.fixture.ClassFinalChild", true, loader)
                    .getDeclaredConstructor().newInstance();
        } catch (LinkageError expected) { newSubclassRejected = true; }
        modifierCheck(report, "class_final_new_subclass_rejected", newSubclassRejected);
        before = commitCalls();
        int removed = redefine(ClassFinalSubject.class, asset(
                "class-nonfinal/com/github/ytlog/artdcevm/fixture/ClassFinalSubject.dex"));
        report.put("classFinalRemoveResult", removed); save(report);
        modifierCheck(report, "class_final_remove_commit", removed == 0 && commitCalls() == before + 1
                && !Modifier.isFinal(ClassFinalSubject.class.getModifiers())
                && subject.value() == 29 && ClassFinalHotCaller.call(subject) == 29);
        ClassLoader childLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "class-final-child/com/github/ytlog/artdcevm/fixture/ClassFinalChild.dex")),
                ClassFinalSubject.class.getClassLoader());
        ClassFinalSubject child = (ClassFinalSubject) Class.forName(
                "com.github.ytlog.artdcevm.fixture.ClassFinalChild", true, childLoader)
                .getDeclaredConstructor().newInstance();
        modifierCheck(report, "class_nonfinal_subclass_dispatch", child.value() == 31
                && ClassFinalHotCaller.call(child) == 31);
        before = commitCalls();
        int conflict = redefine(ClassFinalSubject.class, asset(
                "class-final/com/github/ytlog/artdcevm/fixture/ClassFinalSubject.dex"));
        report.put("classFinalLoadedSubclassResult", conflict); save(report);
        modifierCheck(report, "class_final_loaded_subclass_conflict", conflict == 70
                && commitCalls() == before && !Modifier.isFinal(ClassFinalSubject.class.getModifiers())
                && child.value() == 31 && ClassFinalHotCaller.call(child) == 31);
        report.put("classFinalTransitionPassed", true); save(report);
        return true;
    }
    private boolean testAbstractMethodTransition(JSONObject report) throws Exception {
        report.put("phase", "abstract_method_baseline"); save(report);
        modifierCheck(report, "abstract_method_capability",
                backend.capabilities().getBoolean("abstractMethodTransition"));
        AbstractMethodSubject override = new AbstractMethodOverride();
        Method cached = AbstractMethodSubject.class.getMethod("compute");
        Method hot = AbstractMethodHotCaller.class.getMethod("call", AbstractMethodSubject.class);
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= AbstractMethodHotCaller.call(override);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("abstractCallerJitStateBefore", jit); save(report);
        modifierCheck(report, "abstract_method_baseline", jit == 2
                && Modifier.isAbstract(cached.getModifiers())
                && AbstractMethodHotCaller.call(override) == 31);
        long before = commitCalls();
        int concrete = redefine(AbstractMethodSubject.class, asset(
                "method-concrete/com/github/ytlog/artdcevm/fixture/AbstractMethodSubject.dex"));
        report.put("abstractToConcreteResult", concrete); save(report);
        modifierCheck(report, "abstract_to_concrete_commit", concrete == 0 && commitCalls() == before + 1
                && !Modifier.isAbstract(cached.getModifiers())
                && !Modifier.isAbstract(AbstractMethodSubject.class.getMethod("compute").getModifiers())
                && AbstractMethodHotCaller.call(override) == 31);
        before = commitCalls();
        int abstractAgain = redefine(AbstractMethodSubject.class, asset(
                "method-abstract/com/github/ytlog/artdcevm/fixture/AbstractMethodSubject.dex"));
        report.put("concreteToAbstractResult", abstractAgain); save(report);
        modifierCheck(report, "concrete_to_abstract_commit", abstractAgain == 0
                && commitCalls() == before + 1 && Modifier.isAbstract(cached.getModifiers())
                && AbstractMethodHotCaller.call(override) == 31);
        before = commitCalls();
        int concreteAgain = redefine(AbstractMethodSubject.class, asset(
                "method-concrete/com/github/ytlog/artdcevm/fixture/AbstractMethodSubject.dex"));
        modifierCheck(report, "abstract_to_concrete_restore", concreteAgain == 0
                && commitCalls() == before + 1 && !Modifier.isAbstract(cached.getModifiers())
                && AbstractMethodHotCaller.call(override) == 31);
        ClassLoader inheritedLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "method-inherit/com/github/ytlog/artdcevm/fixture/AbstractMethodInherited.dex")),
                AbstractMethodSubject.class.getClassLoader());
        AbstractMethodSubject inherited = (AbstractMethodSubject) Class.forName(
                "com.github.ytlog.artdcevm.fixture.AbstractMethodInherited", true, inheritedLoader)
                .getDeclaredConstructor().newInstance();
        modifierCheck(report, "concrete_inherited_dispatch", inherited.compute() == 40
                && AbstractMethodHotCaller.call(inherited) == 40
                && (Integer) cached.invoke(inherited) == 40);
        before = commitCalls();
        int conflict = redefine(AbstractMethodSubject.class, asset(
                "method-abstract/com/github/ytlog/artdcevm/fixture/AbstractMethodSubject.dex"));
        report.put("abstractWithDependentSubclassResult", conflict); save(report);
        modifierCheck(report, "abstract_dependent_subclass_conflict", conflict == 71
                && commitCalls() == before && !Modifier.isAbstract(cached.getModifiers())
                && inherited.compute() == 40 && AbstractMethodHotCaller.call(inherited) == 40);
        report.put("abstractMethodTransitionPassed", true); save(report);
        return true;
    }
    private boolean testCombinedAbstractTransition(JSONObject report) throws Exception {
        report.put("phase", "combined_abstract_baseline"); save(report);
        modifierCheck(report, "combined_abstract_capability",
                backend.capabilities().getBoolean("combinedAbstractTransition"));
        CombinedAbstractSubject child = new CombinedAbstractChild();
        Method cached = CombinedAbstractSubject.class.getMethod("compute");
        Method hot = CombinedAbstractHotCaller.class.getMethod("call", CombinedAbstractSubject.class);
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= CombinedAbstractHotCaller.call(child);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("combinedAbstractCallerJitStateBefore", jit); save(report);
        modifierCheck(report, "combined_abstract_baseline", jit == 2
                && Modifier.isAbstract(CombinedAbstractSubject.class.getModifiers())
                && Modifier.isAbstract(cached.getModifiers())
                && CombinedAbstractHotCaller.call(child) == 31);
        long before = commitCalls();
        int concrete = redefine(CombinedAbstractSubject.class, asset(
                "combined-concrete/com/github/ytlog/artdcevm/fixture/CombinedAbstractSubject.dex"));
        report.put("combinedAbstractToConcreteResult", concrete); save(report);
        modifierCheck(report, "combined_abstract_to_concrete", concrete == 0
                && commitCalls() == before + 1
                && !Modifier.isAbstract(CombinedAbstractSubject.class.getModifiers())
                && !Modifier.isAbstract(cached.getModifiers())
                && CombinedAbstractHotCaller.call(child) == 31);
        before = commitCalls();
        int abstractAgain = redefine(CombinedAbstractSubject.class, asset(
                "combined-abstract/com/github/ytlog/artdcevm/fixture/CombinedAbstractSubject.dex"));
        report.put("combinedConcreteToAbstractResult", abstractAgain); save(report);
        modifierCheck(report, "combined_concrete_to_abstract_without_direct", abstractAgain == 0
                && commitCalls() == before + 1
                && Modifier.isAbstract(CombinedAbstractSubject.class.getModifiers())
                && Modifier.isAbstract(cached.getModifiers())
                && !allMethodsDeoptimized() && CombinedAbstractHotCaller.call(child) == 31);
        before = commitCalls();
        int concreteAgain = redefine(CombinedAbstractSubject.class, asset(
                "combined-concrete/com/github/ytlog/artdcevm/fixture/CombinedAbstractSubject.dex"));
        CombinedAbstractSubject direct = CombinedAbstractSubject.class.getConstructor().newInstance();
        modifierCheck(report, "combined_concrete_direct_instance", concreteAgain == 0
                && commitCalls() == before + 1 && direct.compute() == 40
                && CombinedAbstractHotCaller.call(direct) == 40);
        before = commitCalls();
        int conflict = redefine(CombinedAbstractSubject.class, asset(
                "combined-abstract/com/github/ytlog/artdcevm/fixture/CombinedAbstractSubject.dex"));
        report.put("combinedAbstractWithDirectInstanceResult", conflict); save(report);
        modifierCheck(report, "combined_abstract_direct_instance_conflict", conflict == 71
                && commitCalls() == before
                && !Modifier.isAbstract(CombinedAbstractSubject.class.getModifiers())
                && !Modifier.isAbstract(cached.getModifiers())
                && direct.compute() == 40 && CombinedAbstractHotCaller.call(direct) == 40);
        report.put("combinedAbstractTransitionPassed", true); save(report);
        return true;
    }
    private boolean testClassAbstractTransition(JSONObject report) throws Exception {
        report.put("phase", "class_abstract_conservative"); save(report);
        modifierCheck(report, "class_abstract_conservative_capability",
                backend.capabilities().getBoolean("classAbstractConservativeTransition"));
        Constructor<ClassAbstractNoInstance> emptyConstructor =
                ClassAbstractNoInstance.class.getConstructor();
        long before = commitCalls();
        int emptyAbstract = redefine(ClassAbstractNoInstance.class, asset(
                "class-abstract-empty/com/github/ytlog/artdcevm/fixture/ClassAbstractNoInstance.dex"));
        boolean emptyInstantiationRejected = false;
        try { emptyConstructor.newInstance(); }
        catch (InstantiationException expected) { emptyInstantiationRejected = true; }
        report.put("classAbstractWithoutDirectInstanceResult", emptyAbstract); save(report);
        modifierCheck(report, "class_abstract_no_direct_instance", emptyAbstract == 0
                && commitCalls() == before + 1 && emptyInstantiationRejected
                && Modifier.isAbstract(ClassAbstractNoInstance.class.getModifiers())
                && !allMethodsDeoptimized());
        ClassAbstractSubject child = new ClassAbstractChild();
        Method hot = ClassAbstractHotCaller.class.getMethod("call", ClassAbstractSubject.class);
        Constructor<ClassAbstractSubject> cached = ClassAbstractSubject.class.getConstructor();
        int jit = 0;
        for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= ClassAbstractHotCaller.call(child);
            Thread.sleep(100);
            jit = jitMethodState(hot);
        }
        report.put("classAbstractCallerJitStateBefore", jit); save(report);
        modifierCheck(report, "class_abstract_baseline", jit == 2
                && !allMethodsDeoptimized()
                && Modifier.isAbstract(ClassAbstractSubject.class.getModifiers())
                && ClassAbstractHotCaller.call(child) == 29);
        before = commitCalls();
        int concrete = redefine(ClassAbstractSubject.class, asset(
                "class-concrete/com/github/ytlog/artdcevm/fixture/ClassAbstractSubject.dex"));
        report.put("classAbstractToConcreteResult", concrete); save(report);
        modifierCheck(report, "class_abstract_to_concrete", concrete == 0
                && commitCalls() == before + 1
                && !Modifier.isAbstract(ClassAbstractSubject.class.getModifiers())
                && ClassAbstractHotCaller.call(child) == 30);
        ClassAbstractSubject direct = cached.newInstance();
        ClassAbstractSubject[] kept = {direct};
        modifierCheck(report, "class_concrete_new_direct_instance", direct.value() == 30
                && ClassAbstractHotCaller.call(direct) == 30);
        before = commitCalls();
        int abstractAgain = redefine(ClassAbstractSubject.class, asset(
                "class-abstract/com/github/ytlog/artdcevm/fixture/ClassAbstractSubject.dex"));
        report.put("classConcreteToAbstractResult", abstractAgain); save(report);
        JSONObject after = new JSONObject();
        after.put("directVirtual", kept[0].value());
        try { after.put("hotDirect", ClassAbstractHotCaller.call(kept[0])); }
        catch (Throwable error) { after.put("hotDirect", error.toString()); }
        after.put("hotChild", ClassAbstractHotCaller.call(child));
        after.put("jitState", jitMethodState(hot));
        after.put("allMethodsDeoptimized", allMethodsDeoptimized());
        report.put("classAbstractDispatchAfterRestore", after); save(report);
        boolean newInstanceRejected = false;
        try { cached.newInstance(); }
        catch (InstantiationException expected) { newInstanceRejected = true; }
        modifierCheck(report, "class_concrete_to_abstract", abstractAgain == 0
                && commitCalls() == before + 1 && newInstanceRejected && allMethodsDeoptimized()
                && Modifier.isAbstract(ClassAbstractSubject.class.getModifiers())
                && kept[0].value() == 29 && ClassAbstractHotCaller.call(kept[0]) == 29
                && ClassAbstractHotCaller.call(child) == 29);
        report.put("classAbstractTransitionPassed", true); save(report);
        return true;
    }
    private void hierarchyCheck(JSONObject report, String name, boolean passed) throws Exception {
        report.getJSONArray("hierarchyChecks").put(new JSONObject().put("name", name).put("passed", passed));
        save(report);
        if (!passed) throw new AssertionError(name);
    }
    private boolean testInterfaceRemoval(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_removal_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceChild child = new InterfaceChild();
        InterfaceTag oldAlias = subject;
        InterfaceTag[] oldArray = {subject, child};
        subject.own = 41;
        child.own = 51;
        Class<?> identity = InterfaceSubject.class;
        Class<?> childIdentity = InterfaceChild.class;
        Method cachedInterfaceMethod = InterfaceTag.class.getMethod("tag");
        MethodHandle unboundInterfaceMethod = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "tag", MethodType.methodType(int.class));
        MethodHandle boundInterfaceMethod = unboundInterfaceMethod.bindTo(subject);
        HierarchyProbe.retainInterfaceMethod(InterfaceTag.class);
        int jniBefore = HierarchyProbe.interfaceJniInvoke(subject);
        int boundBefore, unboundBefore;
        try {
            boundBefore = (int) boundInterfaceMethod.invokeWithArguments();
            unboundBefore = (int) unboundInterfaceMethod.invokeWithArguments(subject);
        }
        catch (Throwable error) { throw new AssertionError("Interface handle baseline", error); }
        boolean jniFormsBaseline = true;
        for (int fresh = 0; fresh <= 1; ++fresh)
            for (int form = 0; form < 3; ++form)
                jniFormsBaseline &= HierarchyProbe.interfaceJniInvokeForm(subject, form, fresh != 0) == 42;
        hierarchyCheck(report, "interface_removal_baseline",
                InterfaceTag.class.isInstance(subject) && InterfaceTag.class.isInstance(child)
                && InterfaceOldCaller.call(oldAlias) == 42
                && InterfaceOldCaller.isTag(subject)
                && InterfaceOldCaller.castTag(subject) == oldAlias
                && InterfaceOldCaller.call(oldArray[1]) == 52
                && (Integer) cachedInterfaceMethod.invoke(subject) == 42
                && boundBefore == 42 && unboundBefore == 42
                && jniBefore == 42 && jniFormsBaseline);
        Method caller = InterfaceOldCaller.class.getMethod("call", InterfaceTag.class);
        for (int i = 0; i < 100000; ++i) sink ^= InterfaceOldCaller.call(oldAlias);
        int jit = jitMethodState(caller);
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 20000; ++i) sink ^= InterfaceOldCaller.call(oldAlias);
            Thread.sleep(20);
            jit = jitMethodState(caller);
        }
        report.put("interfaceOldCallerJitBefore", jit); save(report);
        hierarchyCheck(report, "interface_old_caller_jit_ready", jit == 2);
        int before = commitCalls();
        long activations = receiverGuardTransactionDiagnostics(0);
        long retentions = receiverGuardTransactionDiagnostics(2);
        int code = redefine(identity, asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        report.put("interfaceRemovalCode", code)
                .put("interfaceRemovalDeoptimized", allMethodsDeoptimized())
                .put("interfaceGuardActivations", receiverGuardTransactionDiagnostics(0) - activations)
                .put("interfaceGuardRetentions", receiverGuardTransactionDiagnostics(2) - retentions); save(report);
        hierarchyCheck(report, "interface_removal_committed",
                code == 0 && commitCalls() == before + 1 && allMethodsDeoptimized()
                && receiverGuardTransactionDiagnostics(0) == activations + 1
                && receiverGuardTransactionDiagnostics(2) == retentions + 1);
        hierarchyCheck(report, "interface_new_graph_and_own_state",
                subject.getClass() == identity && child.getClass() == childIdentity
                && !InterfaceTag.class.isInstance(subject) && !InterfaceTag.class.isInstance(child)
                && !InterfaceOldCaller.isTag(subject) && subject.own == 41 && child.own == 51
                && subject.tag() == 43 && child.tag() == 53);
        boolean castRejected = false;
        boolean oldCallRejected = false;
        boolean arrayCallRejected = false;
        boolean arrayStoreRejected = false;
        boolean reflectionRejected = false;
        boolean[] jniRejected = new boolean[2];
        Throwable handleFailure = null;
        Throwable unboundHandleFailure = null;
        try { InterfaceOldCaller.castTag(subject); }
        catch (ClassCastException expected) { castRejected = true; }
        try { InterfaceOldCaller.call(oldAlias); }
        catch (IncompatibleClassChangeError expected) { oldCallRejected = true; }
        try { InterfaceOldCaller.call(oldArray[1]); }
        catch (IncompatibleClassChangeError expected) { arrayCallRejected = true; }
        try { System.arraycopy(new Object[]{subject}, 0, new InterfaceTag[1], 0, 1); }
        catch (ArrayStoreException expected) { arrayStoreRejected = true; }
        try { cachedInterfaceMethod.invoke(subject); }
        catch (IllegalArgumentException expected) { reflectionRejected = true; }
        try { boundInterfaceMethod.invokeWithArguments(); }
        catch (Throwable error) { handleFailure = error; }
        try { unboundInterfaceMethod.invokeWithArguments(subject); }
        catch (Throwable error) { unboundHandleFailure = error; }
        boolean handleRejected = handleFailure instanceof IllegalStateException
                || handleFailure instanceof ClassCastException
                || handleFailure instanceof IncompatibleClassChangeError;
        boolean unboundHandleRejected = unboundHandleFailure instanceof IllegalStateException
                || unboundHandleFailure instanceof ClassCastException
                || unboundHandleFailure instanceof IncompatibleClassChangeError;
        int jniFormRejectedCount = 0;
        StringBuilder jniFormUnexpected = new StringBuilder();
        int previousJniMode = checkJniMode(-1);
        try {
            for (int mode = 0; mode <= 1; ++mode) {
                checkJniMode(mode);
                try { HierarchyProbe.interfaceJniInvoke(subject); }
                catch (IncompatibleClassChangeError expected) { jniRejected[mode] = true; }
                for (int fresh = 0; fresh <= 1; ++fresh) {
                    for (int form = 0; form < 3; ++form) {
                        try {
                            int value = HierarchyProbe.interfaceJniInvokeForm(subject, form, fresh != 0);
                            jniFormUnexpected.append(mode).append('/').append(fresh).append('/')
                                    .append(form).append(" returned ").append(value).append(';');
                        } catch (IncompatibleClassChangeError expected) {
                            ++jniFormRejectedCount;
                        } catch (Throwable unexpected) {
                            jniFormUnexpected.append(mode).append('/').append(fresh).append('/')
                                    .append(form).append(" threw ")
                                    .append(unexpected.getClass().getName()).append(';');
                        }
                    }
                }
            }
        } finally {
            checkJniMode(previousJniMode);
            HierarchyProbe.releaseInterfaceMethod();
        }
        report.put("interfaceOldCastRejected", castRejected)
                .put("interfaceOldCallRejected", oldCallRejected)
                .put("interfaceArrayCallRejected", arrayCallRejected)
                .put("interfaceArrayStoreRejected", arrayStoreRejected)
                .put("interfaceReflectionRejected", reflectionRejected)
                .put("interfaceJniUncheckedRejected", jniRejected[0])
                .put("interfaceJniCheckedRejected", jniRejected[1])
                .put("interfaceBoundHandleFailure", handleFailure == null ? "none" : handleFailure.getClass().getName())
                .put("interfaceBoundHandleRejected", handleRejected)
                .put("interfaceUnboundHandleFailure", unboundHandleFailure == null ? "none" : unboundHandleFailure.getClass().getName())
                .put("interfaceUnboundHandleRejected", unboundHandleRejected)
                .put("interfaceJniFormRejectedCount", jniFormRejectedCount)
                .put("interfaceJniFormUnexpected", jniFormUnexpected.toString())
                .put("interfaceInvokeReceiverChecks", invokeGuardDiagnostics(0))
                .put("interfaceInvokeReceiverRejections", invokeGuardDiagnostics(1))
                .put("interfaceInvokeFrameMisses", invokeGuardDiagnostics(2)); save(report);
        hierarchyCheck(report, "interface_old_assumptions_rejected",
                castRejected && oldCallRejected && arrayCallRejected && arrayStoreRejected
                && reflectionRejected && handleRejected && unboundHandleRejected
                && jniRejected[0] && jniRejected[1]
                && jniFormRejectedCount == 12 && jniFormUnexpected.length() == 0
                && invokeGuardDiagnostics(1) > 0
                && invokeGuardDiagnostics(2) == 0 && subject.own == 41);
        return true;
    }
    private boolean testInterfaceDefaultShadow(JSONObject report, boolean requireJit) throws Exception {
        if (!report.has("hierarchyChecks")) report.put("hierarchyChecks", new JSONArray());
        report.put("phase", "interface_default_shadow_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceTag oldAlias = subject;
        Method oldMethod = InterfaceTag.class.getMethod("defaultValue");
        MethodHandle oldHandle = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "defaultValue", MethodType.methodType(int.class));
        Method caller = InterfaceOldCaller.class.getMethod("callDefault", InterfaceTag.class);
        HierarchyProbe.retainDefaultInterfaceMethod(InterfaceTag.class);
        int previousJniMode = checkJniMode(-1);
        try {
            checkJniMode(0);
            int jit = jitMethodState(caller);
            if (requireJit) {
                for (int attempt = 0; attempt < 10 && jit != 2; ++attempt) {
                    for (int i = 0; i < 100000; ++i) sink ^= InterfaceOldCaller.callDefault(oldAlias);
                    Thread.sleep(100);
                    jit = jitMethodState(caller);
                }
            }
            report.put("defaultShadowOldCallerJitBefore", jit); save(report);
            hierarchyCheck(report, "interface_default_shadow_baseline",
                    (!requireJit || jit == 2) && InterfaceOldCaller.callDefault(oldAlias) == 77
                    && (Integer) oldMethod.invoke(subject) == 77);
            long before = commitCalls();
            int removed = redefine(InterfaceSubject.class, asset(
                    "hier-interface-remove-shadow/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            report.put("defaultShadowRemoveResult", removed); save(report);
            boolean oldCallRejected = false, oldReflectionRejected = false, oldHandleRejected = false;
            try { InterfaceOldCaller.callDefault(oldAlias); }
            catch (IncompatibleClassChangeError expected) { oldCallRejected = true; }
            try { oldMethod.invoke(subject); }
            catch (IllegalArgumentException expected) { oldReflectionRejected = true; }
            try { oldHandle.invokeWithArguments(subject); }
            catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                oldHandleRejected = true;
            } catch (Throwable error) { throw new AssertionError("Shadow old handle", error); }
            int jniRejected = 0;
            StringBuilder jniUnexpected = new StringBuilder();
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    try {
                        int value = HierarchyProbe.defaultInterfaceJniInvokeForm(
                                subject, form, fresh != 0);
                        jniUnexpected.append(fresh).append('/').append(form)
                                .append(" returned ").append(value).append(';');
                    } catch (IncompatibleClassChangeError expected) { ++jniRejected; }
                    catch (Throwable error) {
                        jniUnexpected.append(fresh).append('/').append(form).append(" threw ")
                                .append(error.getClass().getName()).append(';');
                    }
            Method classMethod = InterfaceSubject.class.getMethod("defaultValue");
            int classValue = (Integer) classMethod.invoke(subject);
            report.put("defaultShadowOldCallRejected", oldCallRejected)
                    .put("defaultShadowOldReflectionRejected", oldReflectionRejected)
                    .put("defaultShadowOldHandleRejected", oldHandleRejected)
                    .put("defaultShadowJniRejected", jniRejected)
                    .put("defaultShadowJniUnexpected", jniUnexpected.toString())
                    .put("defaultShadowClassValue", classValue); save(report);
            hierarchyCheck(report, "interface_default_shadow_old_interface_rejected_new_class_callable",
                    removed == 0 && commitCalls() == before + 1
                    && !InterfaceTag.class.isInstance(subject) && allMethodsDeoptimized()
                    && oldCallRejected && oldReflectionRejected && oldHandleRejected
                    && jniRejected == 8 && jniUnexpected.length() == 0
                    && classMethod.getDeclaringClass() == InterfaceSubject.class && classValue == 88);
            MethodHandle classHandle = MethodHandles.lookup().findVirtual(
                    InterfaceSubject.class, "defaultValue", MethodType.methodType(int.class));
            int registeredIds = StandardJvmtiClient.registerMethodIds(InterfaceSubject.class);
            report.put("defaultShadowRegisteredMethodIds", registeredIds); save(report);
            hierarchyCheck(report, "interface_default_shadow_registered_jni_method_ids", registeredIds > 0);
            before = commitCalls();
            int restored = redefine(InterfaceSubject.class, asset(
                    "hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            int restoredJni = 0;
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    if (HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0) == 77)
                        ++restoredJni;
            int restoredHandleValue;
            try { restoredHandleValue = (int) oldHandle.invokeWithArguments(subject); }
            catch (Throwable error) { throw new AssertionError("Restored default handle", error); }
            String cachedClassMethodOutcome;
            try { cachedClassMethodOutcome = "returned:" + classMethod.invoke(subject); }
            catch (InvocationTargetException error) {
                cachedClassMethodOutcome = "target:" + error.getCause().getClass().getName();
            } catch (Throwable error) {
                cachedClassMethodOutcome = "threw:" + error.getClass().getName();
            }
            String cachedClassHandleOutcome;
            try { cachedClassHandleOutcome = "returned:" + classHandle.invokeWithArguments(subject); }
            catch (Throwable error) {
                cachedClassHandleOutcome = "threw:" + error.getClass().getName();
            }
            Method freshRestored = InterfaceSubject.class.getMethod("defaultValue");
            report.put("defaultShadowRestoreResult", restored)
                    .put("defaultShadowRestoredJni", restoredJni)
                    .put("defaultShadowRestoredHandleValue", restoredHandleValue)
                    .put("defaultShadowCachedClassMethodOutcome", cachedClassMethodOutcome)
                    .put("defaultShadowCachedClassHandleOutcome", cachedClassHandleOutcome)
                    .put("defaultShadowCachedClassMethodDeclaringClass",
                            classMethod.getDeclaringClass().getName())
                    .put("defaultShadowCachedClassMethodModifiers", classMethod.getModifiers())
                    .put("defaultShadowFreshRestoredDeclaringClass", freshRestored.getDeclaringClass().getName());
            save(report);
            hierarchyCheck(report, "interface_default_shadow_restore_dispatch",
                    restored == 0 && commitCalls() == before + 1
                    && InterfaceTag.class.isInstance(subject)
                    && InterfaceOldCaller.callDefault(oldAlias) == 77
                    && (Integer) oldMethod.invoke(subject) == 77
                    && restoredHandleValue == 77 && restoredJni == 8
                    && freshRestored.getDeclaringClass() == InterfaceTag.class
                    && (Integer) freshRestored.invoke(subject) == 77
                    && classMethod.getDeclaringClass() == InterfaceTag.class
                    && "returned:77".equals(cachedClassMethodOutcome)
                    && "returned:77".equals(cachedClassHandleOutcome));
            before = commitCalls();
            int removedAgain = redefine(InterfaceSubject.class, asset(
                    "hier-interface-remove-shadow/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            boolean secondOldCallRejected = false;
            try { InterfaceOldCaller.callDefault(oldAlias); }
            catch (IncompatibleClassChangeError expected) { secondOldCallRejected = true; }
            boolean secondOldReflectionRejected = false, secondOldHandleRejected = false;
            try { oldMethod.invoke(subject); }
            catch (IllegalArgumentException expected) { secondOldReflectionRejected = true; }
            try { oldHandle.invokeWithArguments(subject); }
            catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                secondOldHandleRejected = true;
            } catch (Throwable error) { throw new AssertionError("Second shadow old handle", error); }
            int secondJniRejected = 0;
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    try { HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0); }
                    catch (IncompatibleClassChangeError expected) { ++secondJniRejected; }
            String secondCachedClassMethodOutcome;
            try { secondCachedClassMethodOutcome = "returned:" + classMethod.invoke(subject); }
            catch (InvocationTargetException error) {
                secondCachedClassMethodOutcome = "target:" + error.getCause().getClass().getName();
            } catch (Throwable error) {
                secondCachedClassMethodOutcome = "threw:" + error.getClass().getName();
            }
            String secondCachedClassHandleOutcome;
            try { secondCachedClassHandleOutcome = "returned:" + classHandle.invokeWithArguments(subject); }
            catch (Throwable error) {
                secondCachedClassHandleOutcome = "threw:" + error.getClass().getName();
            }
            Method secondFreshClassMethod = InterfaceSubject.class.getMethod("defaultValue");
            report.put("defaultShadowSecondRemoveResult", removedAgain)
                    .put("defaultShadowSecondOldCallRejected", secondOldCallRejected)
                    .put("defaultShadowSecondOldReflectionRejected", secondOldReflectionRejected)
                    .put("defaultShadowSecondOldHandleRejected", secondOldHandleRejected)
                    .put("defaultShadowSecondJniRejected", secondJniRejected)
                    .put("defaultShadowSecondCachedClassMethodOutcome", secondCachedClassMethodOutcome)
                    .put("defaultShadowSecondCachedClassHandleOutcome", secondCachedClassHandleOutcome)
                    .put("defaultShadowSecondCachedClassMethodDeclaringClass",
                            classMethod.getDeclaringClass().getName())
                    .put("defaultShadowSecondFreshClassValue", secondFreshClassMethod.invoke(subject));
            save(report);
            hierarchyCheck(report, "interface_default_shadow_second_removal",
                    removedAgain == 0 && commitCalls() == before + 1
                    && !InterfaceTag.class.isInstance(subject) && secondOldCallRejected
                    && secondOldReflectionRejected && secondOldHandleRejected
                    && secondJniRejected == 8
                    && secondFreshClassMethod.getDeclaringClass() == InterfaceSubject.class
                    && (Integer) secondFreshClassMethod.invoke(subject) == 88
                    && classMethod.getDeclaringClass() == InterfaceSubject.class
                    && "returned:88".equals(secondCachedClassMethodOutcome)
                    && "returned:88".equals(secondCachedClassHandleOutcome));
            report.put("interfaceDefaultShadowPassed", true).put("phase", "finished"); save(report);
        } finally {
            checkJniMode(previousJniMode);
            HierarchyProbe.releaseDefaultInterfaceMethod();
        }
        return true;
    }
    private boolean testInterfaceDefaultMethod(JSONObject report, boolean checkJniAfterRemoval) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_default_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceTag oldAlias = subject;
        Method cached = InterfaceTag.class.getMethod("defaultValue");
        MethodHandle unbound = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "defaultValue", MethodType.methodType(int.class));
        MethodHandle bound = unbound.bindTo(subject);
        HierarchyProbe.retainDefaultInterfaceMethod(InterfaceTag.class);
        int previousJniMode = checkJniMode(-1);
        try {
            checkJniMode(0);
            int baselineJni = 0;
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form) {
                    int value = HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0);
                    report.put("defaultBaselineJni_" + fresh + "_" + form, value); save(report);
                    if (value == 77) ++baselineJni;
                }
            int baselineBound, baselineUnbound;
            try {
                baselineBound = (int) bound.invokeWithArguments();
                baselineUnbound = (int) unbound.invokeWithArguments(subject);
            } catch (Throwable error) { throw new AssertionError("Default handle baseline", error); }
            hierarchyCheck(report, "interface_default_inherited_dispatch_baseline",
                    InterfaceOldCaller.callDefault(oldAlias) == 77
                    && (Integer) cached.invoke(subject) == 77
                    && baselineBound == 77 && baselineUnbound == 77 && baselineJni == 8);

            int first = redefine(InterfaceSubject.class, asset(
                    "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            int firstJniRejected = 0;
            StringBuilder firstUnexpected = new StringBuilder();
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    try {
                        int value = HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0);
                        firstUnexpected.append(fresh).append('/').append(form)
                                .append(" returned ").append(value).append(';');
                    } catch (IncompatibleClassChangeError expected) { ++firstJniRejected; }
                    catch (Throwable error) {
                        firstUnexpected.append(fresh).append('/').append(form).append(" threw ")
                                .append(error.getClass().getName()).append(';');
                    }
            boolean oldCallRejected = false, reflectionRejected = false;
            boolean boundRejected = false, unboundRejected = false;
            try { InterfaceOldCaller.callDefault(oldAlias); }
            catch (IncompatibleClassChangeError expected) { oldCallRejected = true; }
            try { cached.invoke(subject); }
            catch (IllegalArgumentException expected) { reflectionRejected = true; }
            try { bound.invokeWithArguments(); }
            catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                boundRejected = true;
            } catch (Throwable error) { throw new AssertionError("Default bound handle rejection", error); }
            try { unbound.invokeWithArguments(subject); }
            catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                unboundRejected = true;
            } catch (Throwable error) { throw new AssertionError("Default unbound handle rejection", error); }
            report.put("defaultFirstCode", first)
                    .put("defaultFirstJniRejected", firstJniRejected)
                    .put("defaultFirstUnexpected", firstUnexpected.toString())
                    .put("defaultFirstOldCallRejected", oldCallRejected)
                    .put("defaultFirstReflectionRejected", reflectionRejected)
                    .put("defaultFirstBoundRejected", boundRejected)
                    .put("defaultFirstUnboundRejected", unboundRejected); save(report);
            hierarchyCheck(report, "interface_default_removed_old_references_rejected",
                    first == 0 && firstJniRejected == 8 && firstUnexpected.length() == 0
                    && oldCallRejected && reflectionRejected && boundRejected && unboundRejected
                    && !InterfaceTag.class.isInstance(subject) && allMethodsDeoptimized());

            if (checkJniAfterRemoval) {
                report.put("phase", "interface_default_checkjni_nonvirtual")
                        .put("defaultCheckJniPreviousMode", checkJniMode(-1)); save(report);
                checkJniMode(1);
                report.put("defaultCheckJniActiveMode", checkJniMode(-1)); save(report);
                int checkedRejected = 0;
                StringBuilder checkedUnexpected = new StringBuilder();
                for (int fresh = 0; fresh <= 1; ++fresh)
                    for (int form = 0; form < 4; ++form)
                        try {
                            int value = HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0);
                            checkedUnexpected.append(fresh).append('/').append(form)
                                    .append(" returned ").append(value).append(';');
                        } catch (IncompatibleClassChangeError expected) { ++checkedRejected; }
                        catch (Throwable error) {
                            checkedUnexpected.append(fresh).append('/').append(form).append(" threw ")
                                    .append(error.getClass().getName()).append(';');
                        }
                report.put("defaultCheckJniRejected", checkedRejected)
                        .put("defaultCheckJniUnexpected", checkedUnexpected.toString()); save(report);
                hierarchyCheck(report, "interface_default_checkjni_forms_rejected",
                        checkJniMode(-1) == 1 && checkedRejected == 8
                        && checkedUnexpected.length() == 0);
                checkJniMode(0);
            }

            int restore = redefine(InterfaceSubject.class, asset(
                    "hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            int restoredJni = 0;
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    if (HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0) == 77)
                        ++restoredJni;
            int restoredBound, restoredUnbound;
            try {
                restoredBound = (int) bound.invokeWithArguments();
                restoredUnbound = (int) unbound.invokeWithArguments(subject);
            } catch (Throwable error) { throw new AssertionError("Default handles after restore", error); }
            report.put("defaultRestoreCode", restore).put("defaultRestoredJni", restoredJni)
                    .put("defaultRestoredBound", restoredBound)
                    .put("defaultRestoredUnbound", restoredUnbound); save(report);
            hierarchyCheck(report, "interface_default_restored_cached_references",
                    restore == 0 && InterfaceTag.class.isInstance(subject)
                    && InterfaceOldCaller.callDefault(oldAlias) == 77
                    && (Integer) cached.invoke(subject) == 77 && restoredJni == 8
                    && restoredBound == 77 && restoredUnbound == 77);

            int second = redefine(InterfaceSubject.class, asset(
                    "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            int secondJniRejected = 0;
            for (int fresh = 0; fresh <= 1; ++fresh)
                for (int form = 0; form < 4; ++form)
                    try { HierarchyProbe.defaultInterfaceJniInvokeForm(subject, form, fresh != 0); }
                    catch (IncompatibleClassChangeError expected) { ++secondJniRejected; }
            boolean secondCallRejected = false;
            try { InterfaceOldCaller.callDefault(oldAlias); }
            catch (IncompatibleClassChangeError expected) { secondCallRejected = true; }
            report.put("defaultSecondCode", second)
                    .put("defaultSecondJniRejected", secondJniRejected)
                    .put("defaultSecondOldCallRejected", secondCallRejected); save(report);
            hierarchyCheck(report, "interface_default_second_removal_rejects_again",
                    second == 0 && secondJniRejected == 8 && secondCallRejected
                    && !InterfaceTag.class.isInstance(subject) && allMethodsDeoptimized());
        } finally {
            checkJniMode(previousJniMode);
            HierarchyProbe.releaseDefaultInterfaceMethod();
        }
        return true;
    }
    private boolean testInterfaceDefaultSpecial(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_default_special_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        MethodHandles.Lookup lookup = MethodHandles.privateLookupIn(InterfaceSubject.class, MethodHandles.lookup());
        MethodHandle special = lookup.findSpecial(InterfaceTag.class, "defaultValue",
                MethodType.methodType(int.class), InterfaceSubject.class);
        MethodHandle unreflected = lookup.unreflectSpecial(
                InterfaceTag.class.getMethod("defaultValue"), InterfaceSubject.class);
        MethodHandle boundSpecial = special.bindTo(subject);
        InterfaceSubSubject subSubject = new InterfaceSubSubject();
        MethodHandles.Lookup subLookup = MethodHandles.privateLookupIn(
                InterfaceSubSubject.class, MethodHandles.lookup());
        MethodHandle subinterfaceSpecial = subLookup.findSpecial(InterfaceTag.class,
                "defaultValue", MethodType.methodType(int.class), InterfaceSubSubject.class);
        int baseline;
        int boundBaseline;
        int unreflectedBaseline;
        int subinterfaceBaseline;
        try { baseline = (int) special.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Default special baseline", error); }
        try { boundBaseline = NarrowBoundHandleCaller.callExact(boundSpecial); }
        catch (Throwable error) { throw new AssertionError("Bound default special baseline", error); }
        try { unreflectedBaseline = (int) unreflected.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Unreflected special baseline", error); }
        try { subinterfaceBaseline = (int) subinterfaceSpecial.invokeWithArguments(subSubject); }
        catch (Throwable error) { throw new AssertionError("Subinterface special baseline", error); }
        hierarchyCheck(report, "interface_default_special_baseline",
                baseline == 77 && boundBaseline == 77
                        && unreflectedBaseline == 77 && subinterfaceBaseline == 77);
        Method hotCaller = NarrowBoundHandleCaller.class.getMethod("callExact", MethodHandle.class);
        int jit = 0;
        try {
            for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
                for (int i = 0; i < 100000; ++i)
                    sink ^= NarrowBoundHandleCaller.callExact(boundSpecial);
                Thread.sleep(20);
                jit = jitMethodState(hotCaller);
            }
        } catch (Throwable error) {
            throw new AssertionError("Default special JIT warmup", error);
        }
        report.put("interfaceSpecialHotCallerJitBefore", jit); save(report);
        hierarchyCheck(report, "interface_default_special_hot_caller_compiled", jit == 2);
        int removed = redefine(InterfaceSubject.class, asset(
                "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        String removedOutcome;
        String unreflectedRemovedOutcome;
        String boundRemovedOutcome;
        try { removedOutcome = "returned:" + special.invokeWithArguments(subject); }
        catch (Throwable error) { removedOutcome = "threw:" + error.getClass().getName(); }
        try { unreflectedRemovedOutcome = "returned:" + unreflected.invokeWithArguments(subject); }
        catch (Throwable error) { unreflectedRemovedOutcome = "threw:" + error.getClass().getName(); }
        try { boundRemovedOutcome = "returned:" + NarrowBoundHandleCaller.callExact(boundSpecial); }
        catch (Throwable error) { boundRemovedOutcome = "threw:" + error.getClass().getName(); }
        int subinterfaceAfterRemoval;
        try { subinterfaceAfterRemoval = (int) subinterfaceSpecial.invokeWithArguments(subSubject); }
        catch (Throwable error) { throw new AssertionError("Subinterface after unrelated removal", error); }
        report.put("specialRemoveResult", removed).put("specialRemovedOutcome", removedOutcome)
                .put("unreflectedSpecialRemovedOutcome", unreflectedRemovedOutcome)
                .put("boundSpecialRemovedOutcome", boundRemovedOutcome)
                .put("subinterfaceSpecialAfterRemoval", subinterfaceAfterRemoval); save(report);
        hierarchyCheck(report, "interface_default_special_removed",
                removed == 0 && !InterfaceTag.class.isInstance(subject)
                && "threw:java.lang.IncompatibleClassChangeError".equals(removedOutcome)
                && "threw:java.lang.IncompatibleClassChangeError".equals(unreflectedRemovedOutcome)
                && "threw:java.lang.IncompatibleClassChangeError".equals(boundRemovedOutcome)
                && subinterfaceAfterRemoval == 77);
        int restored = redefine(InterfaceSubject.class, asset(
                "hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        String restoredOutcome;
        String unreflectedRestoredOutcome;
        String boundRestoredOutcome;
        try { restoredOutcome = "returned:" + special.invokeWithArguments(subject); }
        catch (Throwable error) { restoredOutcome = "threw:" + error.getClass().getName(); }
        try { unreflectedRestoredOutcome = "returned:" + unreflected.invokeWithArguments(subject); }
        catch (Throwable error) { unreflectedRestoredOutcome = "threw:" + error.getClass().getName(); }
        try { boundRestoredOutcome = "returned:" + NarrowBoundHandleCaller.callExact(boundSpecial); }
        catch (Throwable error) { boundRestoredOutcome = "threw:" + error.getClass().getName(); }
        report.put("specialRestoreResult", restored).put("specialRestoredOutcome", restoredOutcome)
                .put("unreflectedSpecialRestoredOutcome", unreflectedRestoredOutcome); save(report);
        report.put("boundSpecialRestoredOutcome", boundRestoredOutcome); save(report);
        hierarchyCheck(report, "interface_default_special_restored",
                restored == 0 && InterfaceTag.class.isInstance(subject)
                && "returned:77".equals(restoredOutcome)
                && "returned:77".equals(unreflectedRestoredOutcome));
        hierarchyCheck(report, "interface_default_bound_special_restored",
                "returned:77".equals(boundRestoredOutcome));
        int removedWithShadow = redefine(InterfaceSubject.class, asset(
                "hier-interface-remove-shadow/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        String shadowSpecialOutcome;
        String shadowUnreflectedOutcome;
        String shadowBoundOutcome;
        try { shadowSpecialOutcome = "returned:" + special.invokeWithArguments(subject); }
        catch (Throwable error) { shadowSpecialOutcome = "threw:" + error.getClass().getName(); }
        try { shadowUnreflectedOutcome = "returned:" + unreflected.invokeWithArguments(subject); }
        catch (Throwable error) { shadowUnreflectedOutcome = "threw:" + error.getClass().getName(); }
        try { shadowBoundOutcome = "returned:" + NarrowBoundHandleCaller.callExact(boundSpecial); }
        catch (Throwable error) { shadowBoundOutcome = "threw:" + error.getClass().getName(); }
        int shadowClassValue = (Integer) InterfaceSubject.class.getMethod("defaultValue").invoke(subject);
        report.put("specialShadowRemoveResult", removedWithShadow)
                .put("specialShadowOutcome", shadowSpecialOutcome)
                .put("unreflectedSpecialShadowOutcome", shadowUnreflectedOutcome)
                .put("boundSpecialShadowOutcome", shadowBoundOutcome)
                .put("specialShadowClassValue", shadowClassValue); save(report);
        hierarchyCheck(report, "interface_default_special_shadow_removed",
                removedWithShadow == 0 && !InterfaceTag.class.isInstance(subject)
                && shadowClassValue == 88
                && "threw:java.lang.IncompatibleClassChangeError".equals(shadowSpecialOutcome)
                && "threw:java.lang.IncompatibleClassChangeError".equals(shadowUnreflectedOutcome));
        hierarchyCheck(report, "interface_default_bound_special_shadow_removed",
                "threw:java.lang.IncompatibleClassChangeError".equals(shadowBoundOutcome));
        report.put("interfaceDefaultSpecialPassed", true).put("phase", "finished"); save(report);
        return true;
    }
    private boolean testInterfaceBatch(JSONObject report, boolean includeMember) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_batch_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceChild child = new InterfaceChild();
        InterfacePeer peer = new InterfacePeer();
        Peer memberPeer = new Peer();
        subject.own = 41;
        child.own = 51;
        peer.own = 61;
        InterfaceTag oldSubject = subject, oldChild = child, oldPeer = peer;
        InterfaceTag[] aliases = {oldSubject, oldChild, oldPeer};
        Object[] receivers = {subject, child, peer};
        MethodHandle interfaceHandle = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "tag", MethodType.methodType(int.class));
        HierarchyProbe.retainInterfaceMethod(InterfaceTag.class);
        try {
            hierarchyCheck(report, "interface_batch_baseline",
                    InterfaceOldCaller.call(oldSubject) == 42
                    && InterfaceOldCaller.call(oldChild) == 52
                    && InterfaceOldCaller.call(oldPeer) == 66
                    && HierarchyProbe.interfaceJniInvokeForm(peer, 0, false) == 66
                    && (!includeMember || memberPeer.compute() == 11));
            Method caller = InterfaceOldCaller.class.getMethod("call", InterfaceTag.class);
            int jit = jitMethodState(caller);
            for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
                for (int i = 0; i < 20000; ++i) {
                    sink ^= InterfaceOldCaller.call(oldSubject);
                    sink ^= InterfaceOldCaller.call(oldPeer);
                }
                Thread.sleep(20);
                jit = jitMethodState(caller);
            }
            report.put("interfaceBatchCallerJitBefore", jit); save(report);
            hierarchyCheck(report, "interface_batch_jit_ready", jit == 2);
            long commits = commitCalls();
            long activations = receiverGuardTransactionDiagnostics(0);
            int removed = redefineBatch(includeMember
                            ? new Class<?>[]{InterfaceSubject.class, InterfacePeer.class, Peer.class}
                            : new Class<?>[]{InterfaceSubject.class, InterfacePeer.class},
                    includeMember ? new byte[][]{
                            asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-peer-remove/com/github/ytlog/artdcevm/fixture/InterfacePeer.dex"),
                            asset("peer-added/com/github/ytlog/artdcevm/fixture/Peer.dex")}
                            : new byte[][]{
                            asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-peer-remove/com/github/ytlog/artdcevm/fixture/InterfacePeer.dex")});
            long markerDefault = includeMember ? Peer.class.getField("marker").getLong(memberPeer) : -1;
            report.put("interfaceBatchRemoveResult", removed)
                    .put("interfaceBatchClassCount", includeMember ? 3 : 2)
                    .put("interfaceBatchCommitDelta", commitCalls() - commits)
                    .put("interfaceBatchGuardActivationDelta",
                            receiverGuardTransactionDiagnostics(0) - activations)
                    .put("interfaceBatchAllDeoptimized", allMethodsDeoptimized())
                    .put("interfaceBatchSubjectIsTag", InterfaceTag.class.isInstance(subject))
                    .put("interfaceBatchChildIsTag", InterfaceTag.class.isInstance(child))
                    .put("interfaceBatchPeerIsTag", InterfaceTag.class.isInstance(peer))
                    .put("interfaceBatchSubjectOwn", subject.own)
                    .put("interfaceBatchChildOwn", child.own)
                    .put("interfaceBatchPeerOwn", peer.own)
                    .put("interfaceBatchSubjectTag", subject.tag())
                    .put("interfaceBatchChildTag", child.tag())
                    .put("interfaceBatchPeerTag", peer.tag())
                    .put("interfaceBatchMemberMarkerDefault", markerDefault); save(report);
            hierarchyCheck(report, "interface_batch_removed_graph",
                    removed == 0 && commitCalls() == commits + (includeMember ? 3 : 2)
                    && receiverGuardTransactionDiagnostics(0) == activations + 1
                    && allMethodsDeoptimized()
                    && !InterfaceTag.class.isInstance(subject)
                    && !InterfaceTag.class.isInstance(child)
                    && !InterfaceTag.class.isInstance(peer)
                    && subject.own == 41 && child.own == 51 && peer.own == 61
                    && subject.tag() == 43 && child.tag() == 53 && peer.tag() == 67
                    && (!includeMember || memberPeer.compute() == 12 && markerDefault == 0));
            int oldCallRejected = 0, oldHandleRejected = 0, oldJniRejected = 0;
            for (int i = 0; i < aliases.length; ++i) {
                try { InterfaceOldCaller.call(aliases[i]); }
                catch (IncompatibleClassChangeError expected) { ++oldCallRejected; }
                try { interfaceHandle.invokeWithArguments(receivers[i]); }
                catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                    ++oldHandleRejected;
                } catch (Throwable error) { throw new AssertionError("Batch interface handle", error); }
                try { HierarchyProbe.interfaceJniInvokeForm(receivers[i], 0, false); }
                catch (IncompatibleClassChangeError expected) { ++oldJniRejected; }
            }
            report.put("interfaceBatchOldCallRejected", oldCallRejected)
                    .put("interfaceBatchOldHandleRejected", oldHandleRejected)
                    .put("interfaceBatchOldJniRejected", oldJniRejected); save(report);
            hierarchyCheck(report, "interface_batch_old_references_rejected",
                    oldCallRejected == 3 && oldHandleRejected == 3 && oldJniRejected == 3);
            if (includeMember) Peer.class.getField("marker").setLong(memberPeer, 29);
            commits = commitCalls();
            int restored = redefineBatch(includeMember
                            ? new Class<?>[]{InterfaceSubject.class, InterfacePeer.class, Peer.class}
                            : new Class<?>[]{InterfaceSubject.class, InterfacePeer.class},
                    includeMember ? new byte[][]{
                            asset("hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-peer-restore/com/github/ytlog/artdcevm/fixture/InterfacePeer.dex"),
                            asset("peer-added2/com/github/ytlog/artdcevm/fixture/Peer.dex")}
                            : new byte[][]{
                            asset("hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-peer-restore/com/github/ytlog/artdcevm/fixture/InterfacePeer.dex")});
            long markerRestored = includeMember ? Peer.class.getField("marker").getLong(memberPeer) : -1;
            int restoredHandle;
            try { restoredHandle = (int) interfaceHandle.invokeWithArguments(peer); }
            catch (Throwable error) { throw new AssertionError("Batch restored interface handle", error); }
            report.put("interfaceBatchRestoreResult", restored)
                    .put("interfaceBatchRestoredHandle", restoredHandle)
                    .put("interfaceBatchMemberMarkerRestored", markerRestored); save(report);
            hierarchyCheck(report, "interface_batch_restored_graph",
                    restored == 0 && commitCalls() == commits + (includeMember ? 3 : 2)
                    && InterfaceTag.class.isInstance(subject)
                    && InterfaceTag.class.isInstance(child)
                    && InterfaceTag.class.isInstance(peer)
                    && InterfaceOldCaller.call(oldSubject) == 44
                    && InterfaceOldCaller.call(oldChild) == 54
                    && InterfaceOldCaller.call(oldPeer) == 68
                    && restoredHandle == 68
                    && HierarchyProbe.interfaceJniInvokeForm(peer, 0, false) == 68
                    && subject.own == 41 && child.own == 51 && peer.own == 61
                    && (!includeMember || memberPeer.compute() == 13 && markerRestored == 29));
        } finally {
            HierarchyProbe.releaseInterfaceMethod();
        }
        report.put("interfaceBatchPassed", true).put("phase", "finished"); save(report);
        return true;
    }
    private boolean testInterfaceMixedBatch(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_mixed_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceTag oldAlias = subject;
        Peer peer = new Peer();
        subject.own = 41;
        MethodHandle oldHandle = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "tag", MethodType.methodType(int.class));
        hierarchyCheck(report, "interface_mixed_baseline",
                InterfaceOldCaller.call(oldAlias) == 42 && peer.compute() == 11
                && InterfaceTag.class.isInstance(subject));
        Method caller = InterfaceOldCaller.class.getMethod("call", InterfaceTag.class);
        int jit = jitMethodState(caller);
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 20000; ++i) sink ^= InterfaceOldCaller.call(oldAlias);
            Thread.sleep(20);
            jit = jitMethodState(caller);
        }
        report.put("interfaceMixedCallerJitBefore", jit); save(report);
        hierarchyCheck(report, "interface_mixed_jit_ready", jit == 2);
        long commits = commitCalls();
        long activations = receiverGuardTransactionDiagnostics(0);
        long rollbacks = receiverGuardTransactionDiagnostics(1);
        long retentions = receiverGuardTransactionDiagnostics(2);
        boolean deoptimizedBefore = allMethodsDeoptimized();
        int invalid = redefineBatch(new Class<?>[]{InterfaceSubject.class, Peer.class},
                new byte[][]{
                        asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                        new byte[]{1, 2, 3}});
        report.put("interfaceMixedInvalidResult", invalid)
                .put("interfaceMixedInvalidCommitDelta", commitCalls() - commits)
                .put("interfaceMixedInvalidActivationDelta",
                        receiverGuardTransactionDiagnostics(0) - activations)
                .put("interfaceMixedInvalidRollbackDelta",
                        receiverGuardTransactionDiagnostics(1) - rollbacks)
                .put("interfaceMixedInvalidDeoptimized", allMethodsDeoptimized()); save(report);
        hierarchyCheck(report, "interface_mixed_invalid_atomic_rollback",
                invalid != 0 && commitCalls() == commits
                && receiverGuardTransactionDiagnostics(0) == activations + 1
                && receiverGuardTransactionDiagnostics(1) == rollbacks + 1
                && receiverGuardTransactionDiagnostics(2) == retentions
                && allMethodsDeoptimized() == deoptimizedBefore
                && InterfaceTag.class.isInstance(subject)
                && InterfaceOldCaller.call(oldAlias) == 42
                && peer.compute() == 11);
        commits = commitCalls();
        int removed = redefineBatch(new Class<?>[]{InterfaceSubject.class, Peer.class},
                new byte[][]{
                        asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                        asset("peer-added/com/github/ytlog/artdcevm/fixture/Peer.dex")});
        boolean oldCallRejected = false, oldHandleRejected = false;
        try { InterfaceOldCaller.call(oldAlias); }
        catch (IncompatibleClassChangeError expected) { oldCallRejected = true; }
        try { oldHandle.invokeWithArguments(subject); }
        catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
            oldHandleRejected = true;
        } catch (Throwable error) { throw new AssertionError("Mixed old interface handle", error); }
        long markerDefault = Peer.class.getField("marker").getLong(peer);
        report.put("interfaceMixedRemoveResult", removed)
                .put("interfaceMixedRemoveCommitDelta", commitCalls() - commits)
                .put("interfaceMixedOldCallRejected", oldCallRejected)
                .put("interfaceMixedOldHandleRejected", oldHandleRejected)
                .put("interfaceMixedMarkerDefault", markerDefault); save(report);
        hierarchyCheck(report, "interface_mixed_removed_and_member_added",
                removed == 0 && commitCalls() == commits + 2
                && !InterfaceTag.class.isInstance(subject)
                && oldCallRejected && oldHandleRejected
                && subject.own == 41 && subject.tag() == 43
                && peer.compute() == 12 && markerDefault == 0);
        Peer.class.getField("marker").setLong(peer, 29);
        commits = commitCalls();
        int restored = redefineBatch(new Class<?>[]{InterfaceSubject.class, Peer.class},
                new byte[][]{
                        asset("hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                        asset("peer-added2/com/github/ytlog/artdcevm/fixture/Peer.dex")});
        int restoredHandle;
        try { restoredHandle = (int) oldHandle.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Mixed restored interface handle", error); }
        long markerRestored = Peer.class.getField("marker").getLong(peer);
        report.put("interfaceMixedRestoreResult", restored)
                .put("interfaceMixedRestoreCommitDelta", commitCalls() - commits)
                .put("interfaceMixedRestoredHandle", restoredHandle)
                .put("interfaceMixedMarkerRestored", markerRestored); save(report);
        hierarchyCheck(report, "interface_mixed_restored_and_member_retained",
                restored == 0 && commitCalls() == commits + 2
                && InterfaceTag.class.isInstance(subject)
                && InterfaceOldCaller.call(oldAlias) == 44
                && restoredHandle == 44 && subject.own == 41
                && peer.compute() == 13 && markerRestored == 29);
        report.put("interfaceMixedBatchPassed", true).put("phase", "finished"); save(report);
        return true;
    }
    private boolean testInterfaceTransfer(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_transfer_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceChild child = new InterfaceChild();
        subject.own = 41;
        child.own = 51;
        InterfaceTag oldSubject = subject, oldChild = child;
        MethodHandle oldHandle = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "tag", MethodType.methodType(int.class));
        HierarchyProbe.retainInterfaceMethod(InterfaceTag.class);
        HierarchyProbe.retainDefaultInterfaceMethod(InterfaceTag.class);
        try {
            hierarchyCheck(report, "interface_transfer_baseline",
                    InterfaceOldCaller.call(oldSubject) == 42
                    && InterfaceOldCaller.call(oldChild) == 52
                    && InterfaceOldCaller.callDefault(oldChild) == 77
                    && InterfaceChild.class.getInterfaces().length == 0);
            Method caller = InterfaceOldCaller.class.getMethod("call", InterfaceTag.class);
            int jit = jitMethodState(caller);
            for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
                for (int i = 0; i < 20000; ++i) {
                    sink ^= InterfaceOldCaller.call(oldSubject);
                    sink ^= InterfaceOldCaller.call(oldChild);
                }
                Thread.sleep(20);
                jit = jitMethodState(caller);
            }
            report.put("interfaceTransferCallerJitBefore", jit); save(report);
            hierarchyCheck(report, "interface_transfer_jit_ready", jit == 2);
            long commits = commitCalls();
            int moved = redefineBatch(new Class<?>[]{InterfaceSubject.class, InterfaceChild.class},
                    new byte[][]{
                            asset("hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-child-retain/com/github/ytlog/artdcevm/fixture/InterfaceChild.dex")});
            boolean subjectRejected = false, handleRejected = false, jniRejected = false;
            try { InterfaceOldCaller.call(oldSubject); }
            catch (IncompatibleClassChangeError expected) { subjectRejected = true; }
            try { oldHandle.invokeWithArguments(subject); }
            catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
                handleRejected = true;
            } catch (Throwable error) { throw new AssertionError("Transferred parent handle", error); }
            try { HierarchyProbe.interfaceJniInvokeForm(subject, 0, false); }
            catch (IncompatibleClassChangeError expected) { jniRejected = true; }
            int childHandle;
            try { childHandle = (int) oldHandle.invokeWithArguments(child); }
            catch (Throwable error) { throw new AssertionError("Transferred child handle", error); }
            report.put("interfaceTransferMoveResult", moved)
                    .put("interfaceTransferMoveCommitDelta", commitCalls() - commits)
                    .put("interfaceTransferSubjectRejected", subjectRejected)
                    .put("interfaceTransferHandleRejected", handleRejected)
                    .put("interfaceTransferJniRejected", jniRejected)
                    .put("interfaceTransferChildHandle", childHandle)
                    .put("interfaceTransferChildDirectInterfaces", InterfaceChild.class.getInterfaces().length);
            save(report);
            hierarchyCheck(report, "interface_transfer_parent_loss_child_retention",
                    moved == 0 && commitCalls() == commits + 1
                    && !InterfaceTag.class.isInstance(subject)
                    && InterfaceTag.class.isInstance(child)
                    && InterfaceChild.class.getInterfaces().length == 1
                    && InterfaceChild.class.getInterfaces()[0] == InterfaceTag.class
                    && subjectRejected && handleRejected && jniRejected
                    && subject.own == 41 && child.own == 51
                    && subject.tag() == 43 && child.tag() == 53
                    && InterfaceOldCaller.call(oldChild) == 53
                    && childHandle == 53
                    && InterfaceOldCaller.callDefault(oldChild) == 77
                    && HierarchyProbe.interfaceJniInvokeForm(child, 0, false) == 53
                    && HierarchyProbe.defaultInterfaceJniInvokeForm(child, 0, false) == 77);
            MethodHandles.Lookup childLookup = MethodHandles.privateLookupIn(
                    InterfaceChild.class, MethodHandles.lookup());
            MethodHandle directDefault = childLookup.findSpecial(InterfaceTag.class,
                    "defaultValue", MethodType.methodType(int.class), InterfaceChild.class);
            int directDefaultValue;
            try { directDefaultValue = (int) directDefault.invokeWithArguments(child); }
            catch (Throwable error) { throw new AssertionError("Transferred child special", error); }
            hierarchyCheck(report, "interface_transfer_direct_special_baseline",
                    directDefaultValue == 77);
            commits = commitCalls();
            int inherited = redefineBatch(new Class<?>[]{InterfaceSubject.class, InterfaceChild.class},
                    new byte[][]{
                            asset("hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"),
                            asset("hier-interface-child-inherit/com/github/ytlog/artdcevm/fixture/InterfaceChild.dex")});
            report.put("interfaceTransferRestoreResult", inherited)
                    .put("interfaceTransferRestoreCommitDelta", commitCalls() - commits)
                    .put("interfaceTransferInheritedDirectInterfaces",
                            InterfaceChild.class.getInterfaces().length); save(report);
            String staleDirectOutcome;
            try { staleDirectOutcome = "returned:" + directDefault.invokeWithArguments(child); }
            catch (Throwable error) { staleDirectOutcome = "threw:" + error.getClass().getName(); }
            report.put("interfaceTransferStaleDirectSpecialOutcome", staleDirectOutcome); save(report);
            hierarchyCheck(report, "interface_transfer_restored_inheritance",
                    inherited == 0 && commitCalls() == commits + 1
                    && InterfaceTag.class.isInstance(subject)
                    && InterfaceTag.class.isInstance(child)
                    && InterfaceChild.class.getInterfaces().length == 0
                    && InterfaceOldCaller.call(oldSubject) == 44
                    && InterfaceOldCaller.call(oldChild) == 54
                    && InterfaceOldCaller.callDefault(oldChild) == 77
                    && "threw:java.lang.IncompatibleClassChangeError".equals(staleDirectOutcome)
                    && subject.own == 41 && child.own == 51);
        } finally {
            HierarchyProbe.releaseInterfaceMethod();
            HierarchyProbe.releaseDefaultInterfaceMethod();
        }
        report.put("interfaceTransferPassed", true).put("phase", "finished"); save(report);
        return true;
    }
    private boolean testInterfaceRoundTrip(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_roundtrip_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceChild child = new InterfaceChild();
        InterfaceTag oldAlias = subject;
        subject.own = 41;
        child.own = 51;
        Method cached = InterfaceTag.class.getMethod("tag");
        MethodHandle unbound = MethodHandles.lookup().findVirtual(
                InterfaceTag.class, "tag", MethodType.methodType(int.class));
        MethodHandle bound = unbound.bindTo(subject);
        HierarchyProbe.retainInterfaceMethod(InterfaceTag.class);
        int baselineJniForms = 0;
        for (int fresh = 0; fresh <= 1; ++fresh)
            for (int form = 0; form < 3; ++form)
                if (HierarchyProbe.interfaceJniInvokeForm(subject, form, fresh != 0) == 42)
                    ++baselineJniForms;
        int baselineUnbound;
        try { baselineUnbound = (int) unbound.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Baseline unbound interface handle", error); }
        hierarchyCheck(report, "interface_roundtrip_baseline",
                InterfaceOldCaller.call(oldAlias) == 42
                && (Integer) cached.invoke(subject) == 42
                && HierarchyProbe.interfaceJniInvoke(subject) == 42
                && baselineJniForms == 6 && baselineUnbound == 42);
        long activations = receiverGuardTransactionDiagnostics(0);
        long retentions = receiverGuardTransactionDiagnostics(2);
        int first = redefine(InterfaceSubject.class, asset(
                "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        report.put("interfaceRoundTripFirst", first); save(report);
        boolean firstRejected = false, firstBoundRejected = false, firstUnboundRejected = false;
        try { InterfaceOldCaller.call(oldAlias); }
        catch (IncompatibleClassChangeError expected) { firstRejected = true; }
        try { bound.invokeWithArguments(); }
        catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
            firstBoundRejected = true;
        } catch (Throwable error) { throw new AssertionError("First interface handle rejection", error); }
        try { unbound.invokeWithArguments(subject); }
        catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
            firstUnboundRejected = true;
        } catch (Throwable error) { throw new AssertionError("First unbound interface handle rejection", error); }
        int firstJniRejected = 0;
        for (int fresh = 0; fresh <= 1; ++fresh)
            for (int form = 0; form < 3; ++form)
                try { HierarchyProbe.interfaceJniInvokeForm(subject, form, fresh != 0); }
                catch (IncompatibleClassChangeError expected) { ++firstJniRejected; }
        hierarchyCheck(report, "interface_roundtrip_first_removal",
                first == 0 && firstRejected && firstBoundRejected && firstUnboundRejected
                && firstJniRejected == 6
                && !InterfaceTag.class.isInstance(subject) && !InterfaceTag.class.isInstance(child)
                && subject.own == 41 && child.own == 51
                && subject.tag() == 43 && child.tag() == 53 && allMethodsDeoptimized());
        int restore = redefine(InterfaceSubject.class, asset(
                "hier-interface-restore/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        report.put("interfaceRoundTripRestore", restore); save(report);
        int restoredBound;
        try { restoredBound = (int) bound.invokeWithArguments(); }
        catch (Throwable error) { throw new AssertionError("Restored interface handle", error); }
        int restoredUnbound;
        try { restoredUnbound = (int) unbound.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Restored unbound interface handle", error); }
        int restoredJniForms = 0;
        for (int fresh = 0; fresh <= 1; ++fresh)
            for (int form = 0; form < 3; ++form)
                if (HierarchyProbe.interfaceJniInvokeForm(subject, form, fresh != 0) == 44)
                    ++restoredJniForms;
        hierarchyCheck(report, "interface_roundtrip_restored",
                restore == 0 && InterfaceTag.class.isInstance(subject)
                && InterfaceTag.class.isInstance(child)
                && InterfaceOldCaller.call(oldAlias) == 44
                && InterfaceOldCaller.call((InterfaceTag) (Object) child) == 54
                && (Integer) cached.invoke(subject) == 44
                && restoredBound == 44 && restoredUnbound == 44
                && restoredJniForms == 6 && HierarchyProbe.interfaceJniInvoke(subject) == 44
                && subject.own == 41 && child.own == 51);
        InterfaceSubject fresh = new InterfaceSubject();
        InterfaceTag freshAlias = (InterfaceTag) (Object) fresh;
        hierarchyCheck(report, "interface_roundtrip_fresh_restored",
                InterfaceTag.class.isInstance(fresh) && fresh.own == 17
                && InterfaceOldCaller.call(freshAlias) == 20);
        int second = redefine(InterfaceSubject.class, asset(
                "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
        report.put("interfaceRoundTripSecond", second); save(report);
        boolean secondRejected = false, freshRejected = false, jniRejected = false;
        boolean secondUnboundRejected = false;
        try { InterfaceOldCaller.call(oldAlias); }
        catch (IncompatibleClassChangeError expected) { secondRejected = true; }
        try { InterfaceOldCaller.call(freshAlias); }
        catch (IncompatibleClassChangeError expected) { freshRejected = true; }
        try { HierarchyProbe.interfaceJniInvoke(subject); }
        catch (IncompatibleClassChangeError expected) { jniRejected = true; }
        try { unbound.invokeWithArguments(subject); }
        catch (IllegalStateException | ClassCastException | IncompatibleClassChangeError expected) {
            secondUnboundRejected = true;
        } catch (Throwable error) { throw new AssertionError("Second unbound interface handle rejection", error); }
        int secondJniRejected = 0;
        for (int freshMode = 0; freshMode <= 1; ++freshMode)
            for (int form = 0; form < 3; ++form)
                try { HierarchyProbe.interfaceJniInvokeForm(subject, form, freshMode != 0); }
                catch (IncompatibleClassChangeError expected) { ++secondJniRejected; }
        report.put("interfaceRoundTripOldRejected", secondRejected)
                .put("interfaceRoundTripFreshRejected", freshRejected)
                .put("interfaceRoundTripJniRejected", jniRejected)
                .put("interfaceRoundTripFirstJniFormRejectedCount", firstJniRejected)
                .put("interfaceRoundTripRestoredJniFormCount", restoredJniForms)
                .put("interfaceRoundTripSecondJniFormRejectedCount", secondJniRejected)
                .put("interfaceRoundTripFirstUnboundRejected", firstUnboundRejected)
                .put("interfaceRoundTripRestoredUnbound", restoredUnbound)
                .put("interfaceRoundTripSecondUnboundRejected", secondUnboundRejected)
                .put("interfaceRoundTripFrameMisses", invokeGuardDiagnostics(2)); save(report);
        hierarchyCheck(report, "interface_roundtrip_second_removal",
                second == 0 && secondRejected && freshRejected && jniRejected
                && secondUnboundRejected && secondJniRejected == 6
                && !InterfaceTag.class.isInstance(subject) && !InterfaceTag.class.isInstance(fresh)
                && subject.own == 41 && child.own == 51 && fresh.own == 17
                && subject.tag() == 43 && child.tag() == 53 && fresh.tag() == 19
                && allMethodsDeoptimized() && !fieldReadProbeMode(false, false)
                && receiverGuardTransactionDiagnostics(0) == activations + 1
                && receiverGuardTransactionDiagnostics(2) == retentions + 2
                && invokeGuardDiagnostics(2) == 0);
        HierarchyProbe.releaseInterfaceMethod();
        return true;
    }
    private boolean testInterfaceConcurrent(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "interface_concurrent_baseline"); save(report);
        InterfaceSubject subject = new InterfaceSubject();
        InterfaceTag oldAlias = subject;
        subject.own = 41;
        Method invokeMethod = InterfaceOldCaller.class.getMethod("call", InterfaceTag.class);
        Method typeMethod = InterfaceOldCaller.class.getMethod("isTag", Object.class);
        int invokeJit = 0, typeJit = 0;
        for (int attempt = 0; attempt < 20 && (invokeJit != 2 || typeJit != 2); ++attempt) {
            for (int i = 0; i < 100000; ++i) {
                sink ^= InterfaceOldCaller.call(oldAlias);
                if (InterfaceOldCaller.isTag(subject)) sink ^= 1;
            }
            Thread.sleep(20);
            invokeJit = jitMethodState(invokeMethod);
            typeJit = jitMethodState(typeMethod);
        }
        report.put("interfaceConcurrentInvokeJitBefore", invokeJit)
                .put("interfaceConcurrentTypeJitBefore", typeJit); save(report);
        hierarchyCheck(report, "interface_concurrent_jit_ready", invokeJit == 2 && typeJit == 2);
        AtomicInteger phase = new AtomicInteger(0);
        AtomicInteger invokeBaseline = new AtomicInteger(), typeBaseline = new AtomicInteger();
        AtomicInteger invokeRejected = new AtomicInteger(), typeRejected = new AtomicInteger();
        AtomicInteger wrong = new AtomicInteger(), unexpected = new AtomicInteger();
        Thread invoker = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    int value = InterfaceOldCaller.call(oldAlias);
                    if (state == 1 || value != 42) wrong.incrementAndGet();
                    else invokeBaseline.incrementAndGet();
                } catch (IncompatibleClassChangeError expected) {
                    if (state == 1) invokeRejected.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-old-interface-invoker");
        Thread typeChecker = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    boolean value = InterfaceOldCaller.isTag(subject);
                    if (state == 1 && value) wrong.incrementAndGet();
                    else if (state == 1) typeRejected.incrementAndGet();
                    else if (value) typeBaseline.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-old-interface-type-checker");
        invoker.start(); typeChecker.start();
        int code;
        try {
            long baselineDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((invokeBaseline.get() < 100 || typeBaseline.get() < 100)
                    && System.nanoTime() < baselineDeadline) Thread.sleep(10);
            report.put("interfaceConcurrentBaselineInvoke", invokeBaseline.get())
                    .put("interfaceConcurrentBaselineType", typeBaseline.get()); save(report);
            if (invokeBaseline.get() < 100 || typeBaseline.get() < 100)
                throw new AssertionError("Concurrent interface baseline did not run");
            code = redefine(InterfaceSubject.class, asset(
                    "hier-interface-remove/com/github/ytlog/artdcevm/fixture/InterfaceSubject.dex"));
            phase.set(1);
            report.put("interfaceConcurrentCommitCode", code)
                    .put("interfaceConcurrentDeoptimized", allMethodsDeoptimized()); save(report);
            long rejectionDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((invokeRejected.get() < 100 || typeRejected.get() < 100)
                    && System.nanoTime() < rejectionDeadline) Thread.sleep(10);
        } finally {
            phase.set(2);
            invoker.join(3000); typeChecker.join(3000);
        }
        report.put("interfaceConcurrentInvokeRejected", invokeRejected.get())
                .put("interfaceConcurrentTypeRejected", typeRejected.get())
                .put("interfaceConcurrentWrong", wrong.get())
                .put("interfaceConcurrentUnexpected", unexpected.get())
                .put("interfaceConcurrentInvokerStopped", !invoker.isAlive())
                .put("interfaceConcurrentTypeCheckerStopped", !typeChecker.isAlive())
                .put("interfaceConcurrentFrameMisses", invokeGuardDiagnostics(2)); save(report);
        hierarchyCheck(report, "interface_concurrent_commit_and_rejection",
                code == 0 && allMethodsDeoptimized()
                && invokeRejected.get() >= 100 && typeRejected.get() >= 100
                && wrong.get() == 0 && unexpected.get() == 0
                && !invoker.isAlive() && !typeChecker.isAlive()
                && invokeGuardDiagnostics(2) == 0 && subject.own == 41
                && subject.tag() == 43 && !InterfaceTag.class.isInstance(subject));
        return true;
    }
    private WeakReference<?>[] createTransientLoader(JSONObject report, boolean evolve,
            boolean retireMember) throws Exception {
        String originalAsset = retireMember ? "loader-retired-original" : "loader-original";
        String evolvedAsset = retireMember ? "loader-retired-evolved" : "loader-evolved";
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                originalAsset + "/com/github/ytlog/artdcevm/gc/LoaderSubject.dex")), getTargetContext().getClassLoader());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        Method gone = retireMember ? target.getMethod("gone") : null;
        int before = (Integer) value.invoke(instance);
        int goneBefore = retireMember ? (Integer) gone.invoke(instance) : -1;
        int code = evolve ? redefine(target, asset(
                evolvedAsset + "/com/github/ytlog/artdcevm/gc/LoaderSubject.dex")) : -1;
        int after = (Integer) value.invoke(instance);
        boolean oldMethodRejected = false;
        if (retireMember) {
            try { gone.invoke(instance); }
            catch (NoSuchMethodError expected) { oldMethodRejected = true; }
            catch (InvocationTargetException error) {
                oldMethodRejected = error.getCause() instanceof NoSuchMethodError;
            }
        }
        report.put("loaderBefore", before).put("loaderEvolutionCode", code)
                .put("loaderAfter", after).put("loaderGoneBefore", goneBefore)
                .put("loaderOldMethodRejected", oldMethodRejected); save(report);
        hierarchyCheck(report, "loader_transient_class_executed",
                before == 1 && after == (evolve ? 2 : 1) && (!evolve || code == 0)
                && (!retireMember || (goneBefore == 7 && oldMethodRejected)));
        return new WeakReference<?>[]{new WeakReference<>(loader), new WeakReference<>(target)};
    }
    private boolean testLoaderUnloadDiagnostic(JSONObject report, boolean evolve,
            boolean retireMember) throws Exception {
        report.put("hierarchyChecks", new JSONArray())
                .put("phase", retireMember ? "loader_retired_lifetime_diagnostic"
                        : evolve ? "loader_evolved_unload_diagnostic" : "loader_baseline_unload_diagnostic");
        save(report);
        long deleteCallsBefore = classLoaderDeleteCalls();
        long prunedMethodsBefore = retiredPrunedCount(1);
        WeakReference<?>[] refs = createTransientLoader(report, evolve, retireMember);
        int clearedAt = -1;
        for (int round = 1; round <= 40; ++round) {
            System.gc();
            System.runFinalization();
            Thread.sleep(25);
            if (refs[0].get() == null && refs[1].get() == null
                    && (!retireMember || retiredPrunedCount(1) > prunedMethodsBefore)) {
                clearedAt = round;
                break;
            }
        }
        report.put("loaderWeakCleared", refs[0].get() == null)
                .put("classWeakCleared", refs[1].get() == null)
                .put("loaderClearedAtGcRound", clearedAt)
                .put("classLoaderDeleteHookCallsBefore", deleteCallsBefore)
                .put("classLoaderDeleteHookCallsAfter", classLoaderDeleteCalls())
                .put("retiredMethodsPrunedBefore", prunedMethodsBefore)
                .put("retiredMethodsPrunedAfter", retiredPrunedCount(1))
                .put("loaderDiagnosticOnly", false); save(report);
        hierarchyCheck(report, "loader_retirement_lifetime_safety",
                refs[0].get() == null && refs[1].get() == null
                && (!retireMember || retiredPrunedCount(1) > prunedMethodsBefore));
        return true;
    }
    private WeakReference<?>[] createTransientFieldLoader(JSONObject cycle) throws Exception {
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "loader-field-original/com/github/ytlog/artdcevm/gc/LoaderSubject.dex")),
                getTargetContext().getClassLoader());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        Field gone = target.getField("gone");
        int before = (Integer) value.invoke(instance);
        int fieldBefore = gone.getInt(instance);
        int code = redefine(target, asset(
                "loader-field-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
        int after = (Integer) value.invoke(instance);
        boolean oldFieldRejected = false;
        try { gone.getInt(instance); }
        catch (NoSuchFieldError expected) { oldFieldRejected = true; }
        cycle.put("before", before).put("fieldBefore", fieldBefore)
                .put("evolutionCode", code).put("after", after)
                .put("oldFieldRejected", oldFieldRejected);
        if (before != 1 || fieldBefore != 7 || code != 0 || after != 2 || !oldFieldRejected)
            throw new AssertionError("Retired field access after evolution: " + cycle);
        return new WeakReference<?>[]{new WeakReference<>(loader), new WeakReference<>(target)};
    }
    private boolean testLoaderFieldCycles(JSONObject report, boolean concurrent) throws Exception {
        report.put("hierarchyChecks", new JSONArray())
                .put("phase", concurrent ? "loader_retired_field_concurrent_gc"
                        : "loader_retired_field_cycles"); save(report);
        JSONArray cycles = new JSONArray();
        report.put("loaderFieldCycles", cycles);
        long initialSlots = retiredFieldSlotCount();
        report.put("retiredFieldSlotsBefore", initialSlots);
        AtomicInteger stopGc = new AtomicInteger();
        AtomicInteger backgroundGcCalls = new AtomicInteger();
        Thread gcWorker = concurrent ? new Thread(() -> {
            while (stopGc.get() == 0) {
                System.gc();
                backgroundGcCalls.incrementAndGet();
                try { Thread.sleep(3); }
                catch (InterruptedException ignored) { return; }
            }
        }, "loader-field-gc") : null;
        if (gcWorker != null) gcWorker.start();
        try {
        for (int cycleIndex = 0; cycleIndex < (concurrent ? 32 : 8); ++cycleIndex) {
            JSONObject cycle = new JSONObject().put("index", cycleIndex);
            cycles.put(cycle);
            long pruneBefore = retiredPrunedCount(0);
            long deleteBefore = classLoaderDeleteCalls();
            WeakReference<?>[] refs = createTransientFieldLoader(cycle);
            int clearedAt = -1;
            for (int round = 1; round <= 40; ++round) {
                System.gc();
                System.runFinalization();
                Thread.sleep(25);
                if (refs[0].get() == null && refs[1].get() == null
                        && retiredPrunedCount(0) > pruneBefore) {
                    clearedAt = round;
                    break;
                }
            }
            long pruneAfter = retiredPrunedCount(0);
            long deleteAfter = classLoaderDeleteCalls();
            cycle.put("loaderWeakCleared", refs[0].get() == null)
                    .put("classWeakCleared", refs[1].get() == null)
                    .put("clearedAtGcRound", clearedAt)
                    .put("prunedFieldsBefore", pruneBefore)
                    .put("prunedFieldsAfter", pruneAfter)
                    .put("classLoaderDeleteCallsBefore", deleteBefore)
                    .put("classLoaderDeleteCallsAfter", deleteAfter);
            cycle.put("retiredFieldSlotsAfter", retiredFieldSlotCount());
            save(report);
            hierarchyCheck(report, "loader_retired_field_cycle_" + cycleIndex,
                    clearedAt > 0 && pruneAfter == pruneBefore + 1 && deleteAfter > deleteBefore
                            && retiredFieldSlotCount() == initialSlots + 1);
        }
        } finally {
            stopGc.set(1);
            if (gcWorker != null) {
                gcWorker.interrupt();
                gcWorker.join(5000);
            }
        }
        report.put("loaderBackgroundGcCalls", backgroundGcCalls.get())
                .put("loaderGcWorkerStopped", gcWorker == null || !gcWorker.isAlive()); save(report);
        if (concurrent) hierarchyCheck(report, "loader_field_gc_worker_stopped",
                !gcWorker.isAlive() && backgroundGcCalls.get() > 0);
        return true;
    }
    private WeakReference<?>[] createTransientMixedLoader(JSONObject cycle) throws Exception {
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "loader-mixed-original/com/github/ytlog/artdcevm/gc/LoaderSubject.dex")),
                getTargetContext().getClassLoader());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        Method goneMethod = target.getMethod("goneMethod");
        Field goneField = target.getField("goneField");
        int before = (Integer) value.invoke(instance);
        int methodBefore = (Integer) goneMethod.invoke(instance);
        int fieldBefore = goneField.getInt(instance);
        int code = redefine(target, asset(
                "loader-mixed-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
        int after = (Integer) value.invoke(instance);
        boolean oldMethodRejected = false;
        try { goneMethod.invoke(instance); }
        catch (NoSuchMethodError expected) { oldMethodRejected = true; }
        catch (InvocationTargetException error) {
            oldMethodRejected = error.getCause() instanceof NoSuchMethodError;
        }
        boolean oldFieldRejected = false;
        try { goneField.getInt(instance); }
        catch (NoSuchFieldError expected) { oldFieldRejected = true; }
        cycle.put("before", before).put("methodBefore", methodBefore)
                .put("fieldBefore", fieldBefore).put("evolutionCode", code)
                .put("after", after).put("oldMethodRejected", oldMethodRejected)
                .put("oldFieldRejected", oldFieldRejected);
        if (before != 1 || methodBefore != 9 || fieldBefore != 7 || code != 0
                || after != 2 || !oldMethodRejected || !oldFieldRejected)
            throw new AssertionError("Mixed retirement after evolution: " + cycle);
        return new WeakReference<?>[]{new WeakReference<>(loader), new WeakReference<>(target)};
    }
    private boolean testLoaderMixedRetirement(JSONObject report, boolean concurrent,
            int cycleCount) throws Exception {
        report.put("hierarchyChecks", new JSONArray())
                .put("phase", concurrent ? "loader_mixed_retirement_concurrent_gc"
                        : "loader_mixed_retirement"); save(report);
        JSONArray cycles = new JSONArray();
        JSONArray memorySamples = new JSONArray();
        report.put("loaderMixedCycles", cycles).put("loaderMixedCycleCount", cycleCount)
                .put("loaderMixedMemorySamples", memorySamples);
        long initialSlots = retiredFieldSlotCount();
        AtomicInteger stopGc = new AtomicInteger();
        AtomicInteger backgroundGcCalls = new AtomicInteger();
        Thread gcWorker = concurrent ? new Thread(() -> {
            while (stopGc.get() == 0) {
                System.gc();
                backgroundGcCalls.incrementAndGet();
                try { Thread.sleep(3); }
                catch (InterruptedException ignored) { return; }
            }
        }, "loader-mixed-gc") : null;
        if (gcWorker != null) gcWorker.start();
        try {
        for (int cycleIndex = 0; cycleIndex < cycleCount; ++cycleIndex) {
            JSONObject cycle = new JSONObject().put("index", cycleIndex);
            cycles.put(cycle);
            long fieldsBefore = retiredPrunedCount(0);
            long methodsBefore = retiredPrunedCount(1);
            long deleteBefore = classLoaderDeleteCalls();
            WeakReference<?>[] refs = createTransientMixedLoader(cycle);
            int clearedAt = -1;
            for (int round = 1; round <= 40; ++round) {
                System.gc();
                System.runFinalization();
                Thread.sleep(25);
                if (refs[0].get() == null && refs[1].get() == null
                        && retiredPrunedCount(0) > fieldsBefore
                        && retiredPrunedCount(1) > methodsBefore) {
                    clearedAt = round;
                    break;
                }
            }
            long fieldsAfter = retiredPrunedCount(0);
            long methodsAfter = retiredPrunedCount(1);
            cycle.put("loaderWeakCleared", refs[0].get() == null)
                    .put("classWeakCleared", refs[1].get() == null)
                    .put("clearedAtGcRound", clearedAt)
                    .put("prunedFieldsBefore", fieldsBefore)
                    .put("prunedFieldsAfter", fieldsAfter)
                    .put("prunedMethodsBefore", methodsBefore)
                    .put("prunedMethodsAfter", methodsAfter)
                    .put("classLoaderDeleteCallsBefore", deleteBefore)
                    .put("classLoaderDeleteCallsAfter", classLoaderDeleteCalls())
                    .put("retiredFieldSlotsAfter", retiredFieldSlotCount());
            save(report);
            hierarchyCheck(report, "loader_mixed_retirement_cycle_" + cycleIndex,
                    clearedAt > 0 && fieldsAfter == fieldsBefore + 1
                            && methodsAfter == methodsBefore + 1
                            && classLoaderDeleteCalls() > deleteBefore
                            && retiredFieldSlotCount() == initialSlots + 1);
            if (cycleIndex == 0 || (cycleIndex + 1) % 32 == 0) {
                Runtime runtime = Runtime.getRuntime();
                memorySamples.put(new JSONObject().put("cycle", cycleIndex + 1)
                        .put("nativeHeapBytes", Debug.getNativeHeapAllocatedSize())
                        .put("javaUsedBytes", runtime.totalMemory() - runtime.freeMemory()));
                save(report);
            }
        }
        } finally {
            stopGc.set(1);
            if (gcWorker != null) {
                gcWorker.interrupt();
                gcWorker.join(5000);
            }
        }
        report.put("loaderMixedBackgroundGcCalls", backgroundGcCalls.get())
                .put("loaderMixedGcWorkerStopped", gcWorker == null || !gcWorker.isAlive()); save(report);
        if (concurrent) hierarchyCheck(report, "loader_mixed_gc_worker_stopped",
                !gcWorker.isAlive() && backgroundGcCalls.get() > 0);
        return true;
    }
    private static final class LoaderMixedCaches {
        final WeakReference<?> loaderRef;
        final WeakReference<?> classRef;
        final Method oldMethod;
        final Field oldField;
        final MethodHandle oldMethodHandle;
        final VarHandle oldVarHandle;
        LoaderMixedCaches(ClassLoader loader, Class<?> target, Method method, Field field,
                MethodHandle methodHandle, VarHandle varHandle) {
            loaderRef = new WeakReference<>(loader);
            classRef = new WeakReference<>(target);
            oldMethod = method;
            oldField = field;
            oldMethodHandle = methodHandle;
            oldVarHandle = varHandle;
        }
    }
    private LoaderMixedCaches createCachedMixedLoader(JSONObject report) throws Exception {
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset(
                "loader-mixed-original/com/github/ytlog/artdcevm/gc/LoaderSubject.dex")),
                getTargetContext().getClassLoader());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method oldMethod = target.getMethod("goneMethod");
        Field oldField = target.getField("goneField");
        MethodHandle methodHandle = MethodHandles.lookup().findVirtual(
                target, "goneMethod", MethodType.methodType(int.class));
        VarHandle varHandle = MethodHandles.lookup().findVarHandle(
                target, "goneField", int.class);
        int beforeMethod = (Integer) oldMethod.invoke(instance);
        int beforeField = oldField.getInt(instance);
        int code = redefine(target, asset(
                "loader-mixed-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
        boolean methodRejected = false;
        try { oldMethod.invoke(instance); }
        catch (NoSuchMethodError expected) { methodRejected = true; }
        catch (InvocationTargetException error) {
            methodRejected = error.getCause() instanceof NoSuchMethodError;
        }
        boolean fieldRejected = false;
        try { oldField.getInt(instance); }
        catch (NoSuchFieldError expected) { fieldRejected = true; }
        report.put("cachedBeforeMethod", beforeMethod).put("cachedBeforeField", beforeField)
                .put("cachedEvolutionCode", code).put("cachedOldMethodRejected", methodRejected)
                .put("cachedOldFieldRejected", fieldRejected); save(report);
        if (beforeMethod != 9 || beforeField != 7 || code != 0
                || !methodRejected || !fieldRejected)
            throw new AssertionError("Cached mixed retirement: " + report);
        return new LoaderMixedCaches(loader, target, oldMethod, oldField,
                methodHandle, varHandle);
    }
    private WeakReference<?>[] holdAndReleaseMixedCaches(JSONObject report,
            long fieldsBefore, long methodsBefore) throws Exception {
        LoaderMixedCaches cached = createCachedMixedLoader(report);
        for (int round = 0; round < 3; ++round) {
            System.gc();
            System.runFinalization();
            Thread.sleep(25);
        }
        boolean heldAlive = cached.loaderRef.get() != null && cached.classRef.get() != null;
        boolean unpruned = retiredPrunedCount(0) == fieldsBefore
                && retiredPrunedCount(1) == methodsBefore;
        sink = cached.oldMethod.getName().length() + cached.oldField.getName().length()
                + cached.oldMethodHandle.type().parameterCount()
                + cached.oldVarHandle.coordinateTypes().size();
        report.put("cachedHeldAlive", heldAlive).put("cachedHeldUnpruned", unpruned); save(report);
        if (!heldAlive || !unpruned) throw new AssertionError("Cached metadata was released early");
        return new WeakReference<?>[]{cached.loaderRef, cached.classRef};
    }
    private boolean testLoaderMixedCachedReferences(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray())
                .put("phase", "loader_mixed_cached_references"); save(report);
        long fieldsBefore = retiredPrunedCount(0);
        long methodsBefore = retiredPrunedCount(1);
        long deleteBefore = classLoaderDeleteCalls();
        WeakReference<?>[] refs = holdAndReleaseMixedCaches(report, fieldsBefore, methodsBefore);
        int clearedAt = -1;
        for (int round = 1; round <= 40; ++round) {
            System.gc();
            System.runFinalization();
            Thread.sleep(25);
            if (refs[0].get() == null && refs[1].get() == null
                    && retiredPrunedCount(0) == fieldsBefore + 1
                    && retiredPrunedCount(1) == methodsBefore + 1) {
                clearedAt = round;
                break;
            }
        }
        report.put("cachedReleasedClearRound", clearedAt)
                .put("cachedLoaderWeakCleared", refs[0].get() == null)
                .put("cachedClassWeakCleared", refs[1].get() == null)
                .put("cachedPrunedFieldsAfter", retiredPrunedCount(0))
                .put("cachedPrunedMethodsAfter", retiredPrunedCount(1))
                .put("cachedClassLoaderDeleteCallsAfter", classLoaderDeleteCalls()); save(report);
        hierarchyCheck(report, "loader_cached_metadata_lifetime",
                clearedAt > 0 && classLoaderDeleteCalls() > deleteBefore);
        return true;
    }
    private boolean testAotExperiment(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_experiment"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        int beforeState = aotMethodState(value);
        int enabled = aotExperiment(value, true);
        int activeState = aotMethodState(value);
        int activeValue = -1;
        int restored;
        try {
            if (enabled == 1) activeValue = (Integer) value.invoke(instance);
        } finally {
            restored = aotExperiment(value, false);
        }
        int restoredState = aotMethodState(value);
        int restoredValue = (Integer) value.invoke(instance);
        report.put("aotExperimentBeforeState", beforeState)
                .put("aotExperimentEnableResult", enabled)
                .put("aotExperimentActiveState", activeState)
                .put("aotExperimentActiveValue", activeValue)
                .put("aotExperimentRestoreResult", restored)
                .put("aotExperimentRestoredState", restoredState)
                .put("aotExperimentRestoredValue", restoredValue); save(report);
        hierarchyCheck(report, "aot_experiment_activation_and_restore",
                (beforeState & 1) != 0 && (beforeState & 2) == 0
                        && enabled == 1 && (activeState & 2) != 0
                        && activeValue == 1 && restored == 1
                        && (restoredState & 2) == 0 && restoredValue == 1);
        int code = redefine(target, asset("loader-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
        int after = (Integer) value.invoke(instance);
        report.put("aotExperimentEvolutionCode", code)
                .put("aotExperimentValueAfterEvolution", after); save(report);
        hierarchyCheck(report, "aot_experiment_restored_evolution", code == 0 && after == 2);
        return true;
    }
    private boolean testAotActiveEvolution(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_active_evolution"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        int enabled = aotExperiment(value, true);
        int aotState = aotMethodState(value);
        int oldValue = -1, code = -1, newValue = -1, commitState = -1, restored;
        try {
            if (enabled == 1) {
                oldValue = (Integer) value.invoke(instance);
                report.put("aotActiveBeforeCommitState", aotState)
                        .put("aotActiveOldValue", oldValue); save(report);
                code = redefine(target, asset("loader-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
                Method current = target.getMethod("value");
                commitState = aotMethodState(current);
                newValue = (Integer) current.invoke(instance);
            }
        } finally {
            restored = aotExperiment(target.getMethod("value"), false);
        }
        int restoredState = aotMethodState(target.getMethod("value"));
        report.put("aotActiveEnableResult", enabled)
                .put("aotActiveEvolutionCode", code)
                .put("aotActiveCommitState", commitState)
                .put("aotActiveNewValue", newValue)
                .put("aotActiveRestoreResult", restored)
                .put("aotActiveRestoredState", restoredState); save(report);
        hierarchyCheck(report, "aot_active_evolution_replaces_old_body",
                enabled == 1 && (aotState & 2) != 0 && oldValue == 1
                        && code == 0 && newValue == 2 && (commitState & 2) == 0
                        && restored == 1 && (restoredState & 2) == 0);
        return true;
    }
    private boolean testAotActiveFrame(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_active_frame"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot-active.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot-active.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare active-frame AOT DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.AotActiveSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        java.util.concurrent.atomic.AtomicBoolean gate =
                (java.util.concurrent.atomic.AtomicBoolean) target.getField("release").get(null);
        CountDownLatch entered = (CountDownLatch) target.getField("entered").get(null);
        gate.set(false);
        int enabled = aotExperiment(value, true);
        int aotState = aotMethodState(value);
        AtomicReference<Object> oldResult = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldResult.set(value.invoke(instance)); }
            catch (Throwable error) { oldError.set(error); }
        }, "aot-old-active-frame");
        boolean frameEntered = false;
        int frameState = -1;
        int code = -1, restored;
        try {
            if (enabled == 1) {
                worker.start();
                frameEntered = entered.await(10, TimeUnit.SECONDS);
                if (frameEntered) frameState = aotFrameState(worker, value);
                report.put("aotActiveFrameEntered", frameEntered)
                        .put("aotActiveFrameStateBeforeCommit", aotState)
                        .put("aotActiveFrameSample", frameState); save(report);
                if (frameState == 7) code = redefine(target, asset(
                        "aot-active-evolved/com/github/ytlog/artdcevm/gc/AotActiveSubject.dex"));
            }
        } finally {
            gate.set(true);
            if (worker.isAlive()) worker.join(10000);
            restored = aotExperiment(target.getMethod("value"), false);
        }
        Method current = target.getMethod("value");
        int stateAfter = aotMethodState(current);
        int newValue = (Integer) current.invoke(instance);
        report.put("aotActiveFrameEnableResult", enabled)
                .put("aotActiveFrameEvolutionCode", code)
                .put("aotActiveFrameOldResult", String.valueOf(oldResult.get()))
                .put("aotActiveFrameOldError", String.valueOf(oldError.get()))
                .put("aotActiveFrameWorkerAlive", worker.isAlive())
                .put("aotActiveFrameRestoreResult", restored)
                .put("aotActiveFrameStateAfter", stateAfter)
                .put("aotActiveFrameNewValue", newValue); save(report);
        hierarchyCheck(report, "aot_active_frame_old_then_new_body",
                enabled == 1 && (aotState & 2) != 0 && frameEntered && frameState == 7
                        && code == 0 && !worker.isAlive() && oldError.get() == null
                        && Integer.valueOf(1).equals(oldResult.get())
                        && restored == 1 && (stateAfter & 2) == 0 && newValue == 2);
        return true;
    }
    private boolean testAotSuspendDeopt(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_suspend_deopt"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot-swap.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot-swap.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT parent-swap sidecar before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapSubject", true, loader);
        Class<?> callerClass = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapCaller", true, loader);
        Object subject = target.getConstructor().newInstance();
        Object caller = callerClass.getConstructor().newInstance();
        Method call = callerClass.getMethod("call");
        Field gate = callerClass.getField("release");
        CountDownLatch entered = (CountDownLatch) callerClass.getField("entered").get(null);
        callerClass.getField("stale").set(null, subject);
        gate.setBoolean(null, false);
        long beforeHits = aotSuspendDeoptHits(true);
        int enabled = aotExperiment(call, true);
        int entryState = aotMethodState(call);
        AtomicReference<Object> value = new AtomicReference<>();
        AtomicReference<Throwable> error = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { value.set(call.invoke(caller)); }
            catch (InvocationTargetException thrown) { error.set(thrown.getCause()); }
            catch (Throwable thrown) { error.set(thrown); }
        }, "aot-suspend-deopt-probe");
        boolean frameEntered = false;
        int frameState = -1, frameAfter = -1, request = -1, restored;
        long hits = -1;
        try {
            worker.start();
            frameEntered = entered.await(10, TimeUnit.SECONDS);
            if (frameEntered) frameState = aotFrameState(worker, call);
            report.put("aotSuspendEnabled", enabled).put("aotSuspendEntryState", entryState)
                    .put("aotSuspendFrameEntered", frameEntered)
                    .put("aotSuspendFrameState", frameState); save(report);
            if (enabled == 1 && frameState == 7) request = aotSuspendDeoptProbe(worker, call);
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            do {
                hits = aotSuspendDeoptHits(false) - beforeHits;
                if (hits > 0 || System.nanoTime() >= deadline) break;
                Thread.sleep(10);
            } while (true);
            if (hits > 0 && worker.isAlive()) frameAfter = aotFrameState(worker, call);
        } finally {
            gate.setBoolean(null, true);
            if (worker.isAlive()) worker.join(10000);
            restored = aotExperiment(call, false);
            aotSuspendDeoptHits(true);
        }
        report.put("aotSuspendRequest", request).put("aotSuspendDeoptHits", hits)
                .put("aotSuspendFrameAfter", frameAfter)
                .put("aotSuspendOldValue", String.valueOf(value.get()))
                .put("aotSuspendOldError", String.valueOf(error.get()))
                .put("aotSuspendWorkerAlive", worker.isAlive())
                .put("aotSuspendRestoreResult", restored); save(report);
        hierarchyCheck(report, "aot_compiled_suspend_point_deoptimizes_before_resuming",
                enabled == 1 && (entryState & 2) != 0 && frameEntered && frameState == 7
                        && request == 1 && hits > 0 && (frameAfter & 1) != 0
                        && (frameAfter & 4) == 0 && !worker.isAlive()
                        && Integer.valueOf(14).equals(value.get()) && error.get() == null
                        && restored == 1);
        return true;
    }
    private boolean testAotParentSwap(JSONObject report, boolean twoWorkers,
                                      boolean nonDebuggable) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_parent_swap"); save(report);
        String stem = nonDebuggable ? "artdcevm-aot-swap-optimized" : "artdcevm-aot-swap";
        File dex = new File(getTargetContext().getFilesDir(), stem + ".dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                stem + ".odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT parent-swap DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapSubject", true, loader);
        Class<?> oldParent = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapParent", true, loader);
        Class<?> newParent = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapAlternate", true, loader);
        Class<?> callerClass = Class.forName("com.github.ytlog.artdcevm.gc.AotSwapCaller", true, loader);
        Object subject = target.getConstructor().newInstance();
        Object caller = callerClass.getConstructor().newInstance();
        Object secondCaller = twoWorkers ? callerClass.getConstructor().newInstance() : null;
        Method call = callerClass.getMethod("call");
        Field gate = callerClass.getField("release");
        CountDownLatch entered = (CountDownLatch) callerClass.getField("entered").get(null);
        callerClass.getField("stale").set(null, subject);
        gate.setBoolean(null, false);
        int enabled = aotExperiment(call, true);
        int beforeState = aotMethodState(call);
        AtomicReference<Object> oldResult = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        AtomicReference<Object> secondResult = new AtomicReference<>();
        AtomicReference<Throwable> secondError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldResult.set(call.invoke(caller)); }
            catch (InvocationTargetException error) { oldError.set(error.getCause()); }
            catch (Throwable error) { oldError.set(error); }
        }, "aot-old-parent-caller");
        Thread secondWorker = twoWorkers ? new Thread(() -> {
            try { secondResult.set(call.invoke(secondCaller)); }
            catch (InvocationTargetException error) { secondError.set(error.getCause()); }
            catch (Throwable error) { secondError.set(error); }
        }, "aot-old-parent-caller-two") : null;
        boolean frameEntered = false;
        int frameState = -1, secondFrameState = -1, frameAfter = -1,
                secondFrameAfter = -1, code = -1, restored;
        long beforeDeoptHits = aotSuspendDeoptHits(false);
        long beforeMarks = compiledPrecommitDiagnostics(0);
        long beforeAotBarriers = compiledPrecommitDiagnostics(1);
        long beforeCleanScans = compiledPrecommitDiagnostics(3);
        long beforeUnsupported = compiledPrecommitDiagnostics(4);
        try {
            if (enabled == 1) {
                worker.start();
                frameEntered = entered.await(10, TimeUnit.SECONDS);
                if (frameEntered) frameState = aotFrameState(worker, call);
                if (frameState == 7 && secondWorker != null) {
                    secondWorker.start();
                    for (int attempt = 0; attempt < 100 && secondFrameState != 7; ++attempt) {
                        secondFrameState = aotFrameState(secondWorker, call);
                        if (secondFrameState != 7) Thread.sleep(10);
                    }
                }
                report.put("aotParentSwapEnabled", enabled)
                        .put("aotParentSwapBeforeState", beforeState)
                        .put("aotParentSwapFrameEntered", frameEntered)
                        .put("aotParentSwapFrameState", frameState)
                        .put("aotParentSwapSecondFrameState", secondFrameState); save(report);
                if (frameState == 7 && (!twoWorkers || secondFrameState == 7))
                    code = redefine(target, asset(
                        "aot-swap-evolved/com/github/ytlog/artdcevm/gc/AotSwapSubject.dex"));
                if (code == 0) {
                    frameAfter = aotFrameState(worker, call);
                    if (secondWorker != null) secondFrameAfter = aotFrameState(secondWorker, call);
                }
            }
        } finally {
            gate.setBoolean(null, true);
            if (worker.isAlive()) worker.join(10000);
            if (secondWorker != null && secondWorker.isAlive()) secondWorker.join(10000);
            restored = aotExperiment(call, false);
        }
        int retryCode = code == 66 && !worker.isAlive()
                && (secondWorker == null || !secondWorker.isAlive())
                ? redefine(target, asset("aot-swap-evolved/com/github/ytlog/artdcevm/gc/AotSwapSubject.dex")) : -1;
        long deoptHits = aotSuspendDeoptHits(false) - beforeDeoptHits;
        long marks = compiledPrecommitDiagnostics(0) - beforeMarks;
        long aotBarriers = compiledPrecommitDiagnostics(1) - beforeAotBarriers;
        long cleanScans = compiledPrecommitDiagnostics(3) - beforeCleanScans;
        long unsupported = compiledPrecommitDiagnostics(4) - beforeUnsupported;
        int afterState = aotMethodState(call);
        Throwable laterError = null;
        try { call.invoke(caller); }
        catch (InvocationTargetException error) { laterError = error.getCause(); }
        catch (Throwable error) { laterError = error; }
        int newParentValue = (Integer) target.getMethod("inherited").invoke(subject);
        int newParentField = newParent.getField("alternate").getInt(subject);
        Object fresh = target.getConstructor().newInstance();
        int freshValue = (Integer) target.getMethod("inherited").invoke(fresh);
        report.put("aotParentSwapCode", code)
                .put("aotParentSwapNonDebuggable", nonDebuggable)
                .put("aotParentSwapPrecommitUnsupported", unsupported)
                .put("aotParentSwapSuspendDeoptHits", deoptHits)
                .put("aotParentSwapPrecommitMarks", marks)
                .put("aotParentSwapPrecommitAotBarriers", aotBarriers)
                .put("aotParentSwapPrecommitCleanScans", cleanScans)
                .put("aotParentSwapFrameAfter", frameAfter)
                .put("aotParentSwapSecondFrameAfter", secondFrameAfter)
                .put("aotParentSwapRetryCode", retryCode)
                .put("aotParentSwapOldResult", String.valueOf(oldResult.get()))
                .put("aotParentSwapOldError", String.valueOf(oldError.get()))
                .put("aotParentSwapLaterError", String.valueOf(laterError))
                .put("aotParentSwapWorkerAlive", worker.isAlive())
                .put("aotParentSwapSecondWorkerAlive", secondWorker != null && secondWorker.isAlive())
                .put("aotParentSwapSecondResult", String.valueOf(secondResult.get()))
                .put("aotParentSwapSecondError", String.valueOf(secondError.get()))
                .put("aotParentSwapAfterState", afterState)
                .put("aotParentSwapDeoptimized", allMethodsDeoptimized())
                .put("aotParentSwapRestoreResult", restored)
                .put("aotParentSwapOldParentAssignable", oldParent.isInstance(subject))
                .put("aotParentSwapNewParentAssignable", newParent.isInstance(subject))
                .put("aotParentSwapNewParentField", newParentField)
                .put("aotParentSwapNewParentValue", newParentValue)
                .put("aotParentSwapFreshValue", freshValue); save(report);
        hierarchyCheck(report, nonDebuggable
                        ? "active_nondebug_aot_parent_swap_rejected_then_retried"
                        : twoWorkers
                        ? "two_active_aot_parent_callers_deoptimize_before_commit"
                        : "aot_active_parent_swap_deoptimizes_before_commit",
                nonDebuggable
                ? enabled == 1 && (beforeState & 2) != 0 && frameEntered && frameState == 7
                        && code == 66 && unsupported > 0 && retryCode == 0
                        && !worker.isAlive() && Integer.valueOf(14).equals(oldResult.get())
                        && oldError.get() == null && laterError instanceof IncompatibleClassChangeError
                        && restored == 1 && !oldParent.isInstance(subject)
                        && newParent.isInstance(subject) && newParentField == 0
                        && newParentValue == 10 && freshValue == 39
                : enabled == 1 && (beforeState & 2) != 0 && frameEntered && frameState == 7
                        && (!twoWorkers || secondFrameState == 7)
                        && code == 0 && deoptHits > 0 && cleanScans > 0
                        && (frameAfter & 1) != 0 && (frameAfter & 4) == 0
                        && (!twoWorkers || ((secondFrameAfter & 1) != 0
                                && (secondFrameAfter & 4) == 0))
                        && retryCode == -1 && !worker.isAlive()
                        && (secondWorker == null || !secondWorker.isAlive())
                        && oldResult.get() == null
                        && oldError.get() instanceof IncompatibleClassChangeError
                        && (!twoWorkers || (secondResult.get() == null
                                && secondError.get() instanceof IncompatibleClassChangeError))
                        && laterError instanceof IncompatibleClassChangeError
                        && restored == 1 && (afterState & 2) == 0 && allMethodsDeoptimized()
                        && !oldParent.isInstance(subject) && newParent.isInstance(subject)
                        && newParentField == 0 && newParentValue == 10 && freshValue == 39);
        return true;
    }
    private boolean testAotInterfaceRemoval(JSONObject report, boolean defaultMethod) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_interface_baseline"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot-interface.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot-interface.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT interface DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.AotInterfaceSubject", true, loader);
        Class<?> tag = Class.forName("com.github.ytlog.artdcevm.gc.AotInterfaceTag", true, loader);
        Class<?> callerClass = Class.forName("com.github.ytlog.artdcevm.gc.AotInterfaceCaller", true, loader);
        Object subject = target.getConstructor().newInstance();
        Object caller = callerClass.getConstructor().newInstance();
        Method call = callerClass.getMethod(defaultMethod ? "callDefault" : "call");
        int inheritedDefaultBefore = defaultMethod
                ? (Integer) target.getMethod("defaultValue").invoke(subject) : -1;
        java.util.concurrent.atomic.AtomicBoolean gate =
                (java.util.concurrent.atomic.AtomicBoolean) callerClass.getField(
                        defaultMethod ? "defaultRelease" : "release").get(null);
        CountDownLatch entered = (CountDownLatch) callerClass.getField(
                defaultMethod ? "defaultEntered" : "entered").get(null);
        callerClass.getField("stale").set(null, subject);
        gate.set(false);
        int enabled = aotExperiment(call, true);
        int beforeState = aotMethodState(call);
        AtomicReference<Object> oldResult = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldResult.set(call.invoke(caller)); }
            catch (InvocationTargetException error) { oldError.set(error.getCause()); }
            catch (Throwable error) { oldError.set(error); }
        }, "aot-old-interface-caller");
        boolean frameEntered = false;
        int frameState = -1, code = -1, restored;
        try {
            if (enabled == 1) {
                worker.start();
                frameEntered = entered.await(10, TimeUnit.SECONDS);
                if (frameEntered) frameState = aotFrameState(worker, call);
                report.put("aotInterfaceCallerEnabled", enabled)
                        .put("aotInterfaceCallerBeforeState", beforeState)
                        .put("aotInterfaceFrameEntered", frameEntered)
                        .put("aotInterfaceFrameState", frameState); save(report);
                if (frameState == 7) code = redefine(target, asset(
                        "aot-interface-removed/com/github/ytlog/artdcevm/gc/AotInterfaceSubject.dex"));
            }
        } finally {
            gate.set(true);
            if (worker.isAlive()) worker.join(10000);
            restored = aotExperiment(call, false);
        }
        int retryCode = code == 66 && !worker.isAlive()
                ? redefine(target, asset("aot-interface-removed/com/github/ytlog/artdcevm/gc/AotInterfaceSubject.dex")) : -1;
        int afterState = aotMethodState(call);
        boolean deoptimized = allMethodsDeoptimized();
        Throwable laterError = null;
        try { call.invoke(caller); }
        catch (InvocationTargetException error) { laterError = error.getCause(); }
        catch (Throwable error) { laterError = error; }
        int newClassValue = (Integer) target.getMethod(
                defaultMethod ? "defaultValue" : "tag").invoke(subject);
        report.put("aotInterfaceRemoveCode", code)
                .put("aotInterfaceRetryCode", retryCode)
                .put("aotInterfaceDefaultMethod", defaultMethod)
                .put("aotInterfaceInheritedDefaultBefore", inheritedDefaultBefore)
                .put("aotInterfaceOldResult", String.valueOf(oldResult.get()))
                .put("aotInterfaceOldError", String.valueOf(oldError.get()))
                .put("aotInterfaceLaterError", String.valueOf(laterError))
                .put("aotInterfaceWorkerAlive", worker.isAlive())
                .put("aotInterfaceCallerAfterState", afterState)
                .put("aotInterfaceAllMethodsDeoptimized", deoptimized)
                .put("aotInterfaceRestoreResult", restored)
                .put("aotInterfaceNewClassValue", newClassValue)
                .put("aotInterfaceAssignableAfter", tag.isInstance(subject)); save(report);
        hierarchyCheck(report, defaultMethod
                        ? "aot_active_interface_default_deoptimizes_before_commit"
                        : "aot_active_interface_caller_deoptimizes_before_commit",
                enabled == 1 && (beforeState & 2) != 0 && frameEntered && frameState == 7
                && !worker.isAlive() && code == 0 && retryCode == -1
                && oldResult.get() == null
                && oldError.get() instanceof IncompatibleClassChangeError
                && laterError instanceof IncompatibleClassChangeError
                && restored == 1 && (afterState & 2) == 0 && deoptimized
                && (!defaultMethod || inheritedDefaultBefore == 77)
                && newClassValue == (defaultMethod ? 88 : 19)
                && !tag.isInstance(subject));
        report.put(defaultMethod ? "aotInterfaceDefaultRemovalPassed"
                : "aotInterfaceRemovalPassed", true).put("phase", "finished"); save(report);
        return true;
    }
    private boolean testAotCompiledCaller(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_compiled_caller"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot-active.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot-active.odex");
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare active-frame AOT DEX and odex before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.AotActiveSubject", true, loader);
        Class<?> callerClass = Class.forName("com.github.ytlog.artdcevm.gc.AotActiveCaller", true, loader);
        Object instance = target.getConstructor().newInstance();
        Object callerInstance = callerClass.getConstructor().newInstance();
        Method value = target.getMethod("value");
        Method call = callerClass.getMethod("call", target);
        java.util.concurrent.atomic.AtomicBoolean gate =
                (java.util.concurrent.atomic.AtomicBoolean) target.getField("release").get(null);
        CountDownLatch entered = (CountDownLatch) target.getField("entered").get(null);
        gate.set(false);
        int enabledTarget = aotExperiment(value, true);
        int enabledCaller = enabledTarget == 1 ? aotExperiment(call, true) : -1;
        int targetBefore = aotMethodState(value), callerBefore = aotMethodState(call);
        AtomicReference<Object> oldResult = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldResult.set(call.invoke(callerInstance, instance)); }
            catch (Throwable error) { oldError.set(error); }
        }, "aot-compiled-caller");
        boolean enteredFrame = false;
        int targetFrame = -1, callerFrame = -1, code = -1;
        int callerAfter = -1, targetAfter = -1, newValue = -1;
        int restoredCaller = -1, restoredTarget;
        try {
            if (enabledTarget == 1 && enabledCaller == 1) {
                worker.start();
                enteredFrame = entered.await(10, TimeUnit.SECONDS);
                if (enteredFrame) {
                    targetFrame = aotFrameState(worker, value);
                    callerFrame = aotFrameState(worker, call);
                }
                report.put("aotCallerTargetFrame", targetFrame)
                        .put("aotCallerFrame", callerFrame).put("aotCallerEntered", enteredFrame); save(report);
                if (targetFrame == 7 && callerFrame == 7) {
                    code = redefine(target, asset(
                            "aot-active-evolved/com/github/ytlog/artdcevm/gc/AotActiveSubject.dex"));
                    gate.set(true);
                    worker.join(10000);
                    callerAfter = aotMethodState(call);
                    targetAfter = aotMethodState(target.getMethod("value"));
                    if (code == 0 && !worker.isAlive())
                        newValue = (Integer) call.invoke(callerInstance, instance);
                }
            }
        } finally {
            gate.set(true);
            if (worker.isAlive()) worker.join(10000);
            if (enabledCaller == 1) restoredCaller = aotExperiment(call, false);
            restoredTarget = aotExperiment(target.getMethod("value"), false);
        }
        report.put("aotCallerTargetEnable", enabledTarget).put("aotCallerEnable", enabledCaller)
                .put("aotCallerTargetBefore", targetBefore).put("aotCallerBefore", callerBefore)
                .put("aotCallerEvolutionCode", code).put("aotCallerAfter", callerAfter)
                .put("aotCallerTargetAfter", targetAfter)
                .put("aotCallerOldResult", String.valueOf(oldResult.get()))
                .put("aotCallerOldError", String.valueOf(oldError.get()))
                .put("aotCallerWorkerAlive", worker.isAlive())
                .put("aotCallerNewValue", newValue)
                .put("aotCallerRestore", restoredCaller).put("aotCallerTargetRestore", restoredTarget);
        save(report);
        hierarchyCheck(report, "aot_compiled_caller_old_frame_and_new_target",
                enabledTarget == 1 && enabledCaller == 1 && (targetBefore & 2) != 0
                        && (callerBefore & 2) != 0 && enteredFrame && targetFrame == 7
                        && callerFrame == 7 && code == 0 && !worker.isAlive()
                        && oldError.get() == null && Integer.valueOf(1).equals(oldResult.get())
                        && (callerAfter & 2) != 0 && (targetAfter & 2) == 0
                        && newValue == 2 && restoredCaller == 1 && restoredTarget == 1);
        return true;
    }
    private boolean testAotLoaderEvolution(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "aot_loader_evolution"); save(report);
        File dex = new File(getTargetContext().getFilesDir(), "artdcevm-aot.dex");
        File odex = new File(new File(new File(dex.getParentFile(), "oat"), "arm64"),
                "artdcevm-aot.odex");
        report.put("aotDexPresent", dex.isFile()).put("aotOdexPresent", odex.isFile()); save(report);
        if (!dex.isFile() || !odex.isFile())
            throw new AssertionError("Prepare AOT DEX and odex in app files before this suite");
        ClassLoader loader = new DexClassLoader(dex.getAbsolutePath(),
                getTargetContext().getCodeCacheDir().getAbsolutePath(), null,
                getTargetContext().getClassLoader().getParent());
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.gc.LoaderSubject", true, loader);
        Object instance = target.getConstructor().newInstance();
        Method value = target.getMethod("value");
        int aotBefore = aotMethodState(value);
        int before = (Integer) value.invoke(instance);
        int code = redefine(target, asset(
                "loader-evolved/com/github/ytlog/artdcevm/gc/LoaderSubject.dex"));
        int after = (Integer) value.invoke(instance);
        int aotAfter = aotMethodState(value);
        report.put("aotMethodStateBefore", aotBefore)
                .put("aotMethodStateAfter", aotAfter)
                .put("aotValueBefore", before).put("aotEvolutionCode", code)
                .put("aotValueAfter", after).put("aotLoaderType", loader.getClass().getName())
                .put("aotExecutableOnDebugRuntime", (aotBefore & 2) != 0);
        save(report);
        hierarchyCheck(report, "aot_code_present_but_debug_runtime_uses_nterp",
                (aotBefore & 1) != 0 && (aotBefore & 2) == 0
                        && (aotBefore & 16) != 0 && before == 1);
        hierarchyCheck(report, "aot_evolution_commits_and_changes_old_method",
                code == 0 && after == 2);
        return true;
    }
    private boolean testJitInlineEvolution(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "jit_inline_evolution"); save(report);
        InlineCallee callee = new InlineCallee();
        Method caller = InlineHotCaller.class.getMethod("call", InlineCallee.class, int.class);
        int before = InlineHotCaller.call(callee, 40);
        int inlineState = jitInlineState(caller);
        for (int attempt = 0; attempt < 25 && inlineState != 3; ++attempt) {
            for (int i = 0; i < 40000; ++i) sink ^= InlineHotCaller.call(callee, i);
            Thread.sleep(20);
            inlineState = jitInlineState(caller);
        }
        report.put("inlineBefore", before).put("inlineCallerJitStateBefore", jitMethodState(caller))
                .put("inlineCallerStateBefore", inlineState); save(report);
        hierarchyCheck(report, "jit_debug_graph_compiled_without_inline_info",
                before == 41 && jitMethodState(caller) == 2 && inlineState == 2);
        int code = redefine(InlineCallee.class, asset(
                "inline-evolved/com/github/ytlog/artdcevm/fixture/InlineCallee.dex"));
        int stateAfter = jitInlineState(caller);
        int after = InlineHotCaller.call(callee, 40);
        report.put("inlineEvolutionCode", code).put("inlineCallerStateAfter", stateAfter)
                .put("inlineAfter", after).put("inlineAllMethodsDeoptimized", allMethodsDeoptimized());
        save(report);
        hierarchyCheck(report, "jit_compiled_caller_observes_new_body",
                code == 0 && after == 42 && stateAfter != 3);
        return true;
    }
    private boolean testJitOptimizedInlineEvolution(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "jit_optimized_inline_evolution");
        save(report);
        InlineCallee callee = new InlineCallee();
        Method caller = InlineHotCaller.class.getMethod("call", InlineCallee.class, int.class);
        Method value = InlineCallee.class.getMethod("value", int.class);
        int enabled = jitInlineExperiment(true);
        int optionsState = jitCompilerOptionsState();
        int inPlaceBefore = inPlaceInvalidationCalls();
        int structuralBefore = commitCalls(), structuralAfter = -1;
        int before = InlineHotCaller.call(callee, 40);
        int stateBefore = -1, codeKindBefore = -1, forced = -1, forcedInitial = -1;
        int stateInitial = -1, codeKindInitial = -1, cacheInitial = -1;
        int code = -1, stateAfter = -1, after = -1, directAfter = -1, restored, probeRemoved = -1;
        int manualInvalidation = -1, cacheAfterManual = -1, afterManual = -1;
        int probeInstalled = -1, cacheBefore = -1, cacheAfter = -1, compiledBefore = -1;
        int inPlaceAfter = -1;
        long inlineHits = -1;
        try {
            if (enabled == 1) {
                probeInstalled = configureGraphInlineProbe(caller, value, true);
                if (probeInstalled == 1) {
                    forcedInitial = forceOptimizedJit(caller);
                    stateBefore = jitInlineState(caller);
                    codeKindBefore = jitCodeKind(caller);
                    stateInitial = stateBefore;
                    codeKindInitial = codeKindBefore;
                    cacheInitial = jitCacheDetail(caller);
                    inlineHits = graphInlineHits();
                    for (int attempt = 0; attempt < 25 && inlineHits == 0; ++attempt) {
                        // Reflection keeps the outer harness from absorbing
                        // this tiny caller before it becomes JIT hot itself.
                        for (int i = 0; i < 10000; ++i)
                            sink ^= (Integer) caller.invoke(null, callee, i);
                        Thread.sleep(20);
                        forced = forceOptimizedJit(caller);
                        stateBefore = jitInlineState(caller);
                        codeKindBefore = jitCodeKind(caller);
                        inlineHits = graphInlineHits();
                    }
                    cacheBefore = jitCacheDetail(caller);
                    compiledBefore = InlineHotCaller.call(callee, 40);
                    report.put("optimizedInlineStateBefore", stateBefore)
                            .put("optimizedInlineCodeKindBefore", codeKindBefore)
                            .put("optimizedInlineCacheBefore", cacheBefore)
                            .put("optimizedInlineGraphHits", inlineHits)
                            .put("optimizedInlineForceResult", forced)
                            .put("optimizedInlineJitStateBefore", jitMethodState(caller)); save(report);
                    if (inlineHits > 0 && (cacheBefore & 16) != 0 && compiledBefore == 41) {
                        code = redefine(InlineCallee.class, asset(
                                "inline-evolved/com/github/ytlog/artdcevm/fixture/InlineCallee.dex"));
                        inPlaceAfter = inPlaceInvalidationCalls();
                        structuralAfter = commitCalls();
                        stateAfter = jitInlineState(caller);
                        cacheAfter = jitCacheDetail(caller);
                        after = InlineHotCaller.call(callee, 40);
                        directAfter = callee.value(40);
                        if (code == 0 && after == 41) {
                            manualInvalidation = invalidateAllJit();
                            cacheAfterManual = jitCacheDetail(caller);
                            afterManual = InlineHotCaller.call(callee, 40);
                        }
                    }
                }
            }
        } finally {
            if (probeInstalled == 1) probeRemoved = configureGraphInlineProbe(caller, value, false);
            restored = jitInlineExperiment(false);
        }
        report.put("optimizedInlineEnable", enabled).put("optimizedInlineBefore", before)
                .put("optimizedInlineProbeInstalled", probeInstalled)
                .put("optimizedInlineProbeRemoved", probeRemoved)
                .put("optimizedInlineGraphHits", inlineHits)
                .put("optimizedInlineCompiledBefore", compiledBefore)
                .put("optimizedInlineStateBefore", stateBefore)
                .put("optimizedInlineCodeKindBefore", codeKindBefore)
                .put("optimizedInlineCacheBefore", cacheBefore)
                .put("optimizedInlineCacheAfter", cacheAfter)
                .put("optimizedInlineDirectAfter", directAfter)
                .put("optimizedInlineManualInvalidation", manualInvalidation)
                .put("optimizedInlineCacheAfterManual", cacheAfterManual)
                .put("optimizedInlineAfterManual", afterManual)
                .put("optimizedInlineForceResult", forced)
                .put("optimizedInlineInitialForceResult", forcedInitial)
                .put("optimizedInlineInitialState", stateInitial)
                .put("optimizedInlineInitialCodeKind", codeKindInitial)
                .put("optimizedInlineInitialCache", cacheInitial)
                .put("optimizedInlineCompilerOptions", optionsState)
                .put("optimizedInlineInPlaceBefore", inPlaceBefore)
                .put("optimizedInlineInPlaceAfter", inPlaceAfter)
                .put("optimizedInlineStructuralBefore", structuralBefore)
                .put("optimizedInlineStructuralAfter", structuralAfter)
                .put("optimizedInlineEvolutionCode", code)
                .put("optimizedInlineStateAfter", stateAfter)
                .put("optimizedInlineAfter", after)
                .put("optimizedInlineRestore", restored); save(report);
        hierarchyCheck(report, "optimized_jit_inline_caller_invalidated_after_evolution",
                enabled == 1 && optionsState == 5 && before == 41 && probeInstalled == 1
                        && inlineHits > 0 && (cacheBefore & 16) != 0 && compiledBefore == 41
                        && code == 0 && structuralAfter > structuralBefore
                        && inPlaceAfter == inPlaceBefore
                        && (cacheAfter & 16) == 0 && after == 42
                        && probeRemoved == 1 && restored == 1);
        return true;
    }
    private boolean testBootJitInlineDiagnostic(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "boot_jit_inline_diagnostic"); save(report);
        ArrayList<Object> items = new ArrayList<>();
        items.add(40);
        BootInlineConsumer consumer = new BootInlineConsumer();
        Method caller = ArrayList.class.getMethod("forEach", Consumer.class);
        Method callee = BootInlineConsumer.class.getMethod("accept", Object.class);
        int baseline = -1, enabled = -1, probe = -1, force = -1, cache = -1;
        int frameState = -1, removed = -1, restored = -1;
        long inlineHits = -1;
        boolean entered = false;
        AtomicReference<Throwable> workerError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try {
                ArrayList<Object> blocked = new ArrayList<>();
                blocked.add(-1);
                blocked.forEach(consumer);
            } catch (Throwable error) {
                workerError.set(error);
            }
        }, "boot-jit-inline-worker");
        try {
            items.forEach(consumer);
            baseline = consumer.last;
            for (int i = 0; i < 50000; ++i) items.forEach(consumer);
            enabled = jitInlineExperiment(true);
            probe = enabled == 1 ? configureGraphInlineProbe(caller, callee, true) : -1;
            if (probe == 1) {
                for (int attempt = 0; attempt < 16; ++attempt) {
                    for (int i = 0; i < 20000; ++i) items.forEach(consumer);
                    invalidateAllJit();
                    force = forceOptimizedJit(caller);
                    inlineHits = graphInlineHits();
                    cache = jitCacheDetail(caller);
                    if (inlineHits > 0 && (cache & 16) != 0) break;
                }
                if (inlineHits > 0 && (cache & 16) != 0) {
                    worker.start();
                    entered = BootInlineConsumer.entered.await(10, TimeUnit.SECONDS);
                    if (entered) {
                        for (int attempt = 0; attempt < 100; ++attempt) {
                            frameState = jitActiveFrameState(worker, caller, callee);
                            if (frameState == 15) break;
                            Thread.sleep(10);
                        }
                    }
                }
            }
        } finally {
            BootInlineConsumer.release.set(true);
            if (worker.isAlive()) worker.join(10000);
            if (probe == 1) removed = configureGraphInlineProbe(caller, callee, false);
            if (enabled == 1) restored = jitInlineExperiment(false);
        }
        report.put("bootInlineCaller", caller.getDeclaringClass().getName())
                .put("bootInlineBaseline", baseline).put("bootInlineEnabled", enabled)
                .put("bootInlineProbe", probe).put("bootInlineForce", force)
                .put("bootInlineGraphHits", inlineHits).put("bootInlineCache", cache)
                .put("bootInlineEntered", entered).put("bootInlineFrameState", frameState)
                .put("bootInlineWorkerError", String.valueOf(workerError.get()))
                .put("bootInlineWorkerAlive", worker.isAlive())
                .put("bootInlineProbeRemoved", removed).put("bootInlineRestored", restored); save(report);
        report.put("bootInlineObserved", inlineHits > 0 && frameState == 15); save(report);
        hierarchyCheck(report, "boot_jit_inline_diagnostic_executed",
                baseline == 41 && enabled == 1 && probe == 1 && force == 1
                        && (cache & 16) != 0 && workerError.get() == null && !worker.isAlive()
                        && removed == 1 && restored == 1);
        return true;
    }
    private boolean testJitActiveInlineFrame(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "jit_active_inline_frame"); save(report);
        long deoptHitsBefore = aotSuspendDeoptHits(false);
        long[] precommitBefore = new long[6];
        for (int index = 0; index < precommitBefore.length; ++index)
            precommitBefore[index] = compiledPrecommitDiagnostics(index);
        InlineActiveCallee callee = new InlineActiveCallee();
        Method caller = InlineActiveCaller.class.getMethod(
                "call", InlineActiveCallee.class, int.class);
        Method value = InlineActiveCallee.class.getMethod("value", int.class);
        InlineActiveCallee.release.set(false);
        int before = InlineActiveCaller.call(callee, 40);
        int enabled = jitInlineExperiment(true);
        int probe = enabled == 1 ? configureGraphInlineProbe(caller, value, true) : -1;
        int force = -1, cacheBefore = -1, frameState = -1, secondFrameState = -1;
        int code = -1, cacheAfter = -1;
        int newValue = -1, removed = -1, restored, inPlaceAfter = -1;
        int inPlaceBefore = inPlaceInvalidationCalls();
        int structuralBefore = commitCalls(), structuralAfter = -1;
        long inlineHits = -1;
        boolean entered = false;
        AtomicReference<Object> oldResult = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        AtomicReference<Object> secondOldResult = new AtomicReference<>();
        AtomicReference<Throwable> secondOldError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldResult.set(InlineActiveCaller.call(callee, -1)); }
            catch (Throwable error) { oldError.set(error); }
        }, "jit-active-inline-1");
        Thread secondWorker = new Thread(() -> {
            try { secondOldResult.set(InlineActiveCaller.call(callee, -1)); }
            catch (Throwable error) { secondOldError.set(error); }
        }, "jit-active-inline-2");
        try {
            if (probe == 1) {
                force = forceOptimizedJit(caller);
                inlineHits = graphInlineHits();
                for (int attempt = 0; attempt < 8 && inlineHits == 0; ++attempt) {
                    for (int i = 0; i < 10000; ++i)
                        sink ^= (Integer) caller.invoke(null, callee, 40);
                    invalidateAllJit();
                    force = forceOptimizedJit(caller);
                    inlineHits = graphInlineHits();
                }
                cacheBefore = jitCacheDetail(caller);
                report.put("activeInlineGraphHits", inlineHits)
                        .put("activeInlineCacheBefore", cacheBefore)
                        .put("activeInlineForce", force); save(report);
                if (inlineHits > 0 && (cacheBefore & 16) != 0) {
                    worker.start();
                    secondWorker.start();
                    entered = InlineActiveCallee.entered.await(10, TimeUnit.SECONDS);
                    if (entered) {
                        for (int attempt = 0; attempt < 100; ++attempt) {
                            frameState = jitActiveFrameState(worker, caller, value);
                            secondFrameState = jitActiveFrameState(secondWorker, caller, value);
                            if (frameState == 15 && secondFrameState == 15) break;
                            Thread.sleep(10);
                        }
                    }
                    report.put("activeInlineEntered", entered)
                            .put("activeInlineFrameState", frameState)
                            .put("activeInlineSecondFrameState", secondFrameState); save(report);
                    if (frameState == 15 && secondFrameState == 15) {
                        code = redefine(InlineActiveCallee.class, asset(
                                "inline-active-evolved/com/github/ytlog/artdcevm/fixture/InlineActiveCallee.dex"));
                        inPlaceAfter = inPlaceInvalidationCalls();
                        structuralAfter = commitCalls();
                        cacheAfter = jitCacheDetail(caller);
                    }
                }
            }
        } finally {
            InlineActiveCallee.release.set(true);
            if (worker.isAlive()) worker.join(10000);
            if (secondWorker.isAlive()) secondWorker.join(10000);
            if (probe == 1) removed = configureGraphInlineProbe(caller, value, false);
            restored = jitInlineExperiment(false);
        }
        if (code == 0 && !worker.isAlive())
            newValue = InlineActiveCaller.call(callee, -1);
        report.put("activeInlineBefore", before).put("activeInlineEnable", enabled)
                .put("activeInlineProbe", probe).put("activeInlineGraphHits", inlineHits)
                .put("activeInlineCacheBefore", cacheBefore).put("activeInlineFrameState", frameState)
                .put("activeInlineSecondFrameState", secondFrameState)
                .put("activeInlineEntered", entered).put("activeInlineEvolutionCode", code)
                .put("activeInlineInPlaceBefore", inPlaceBefore)
                .put("activeInlineInPlaceAfter", inPlaceAfter)
                .put("activeInlineStructuralBefore", structuralBefore)
                .put("activeInlineStructuralAfter", structuralAfter)
                .put("activeInlineCacheAfter", cacheAfter)
                .put("activeInlineOldResult", String.valueOf(oldResult.get()))
                .put("activeInlineOldError", String.valueOf(oldError.get()))
                .put("activeInlineWorkerAlive", worker.isAlive())
                .put("activeInlineSecondOldResult", String.valueOf(secondOldResult.get()))
                .put("activeInlineSecondOldError", String.valueOf(secondOldError.get()))
                .put("activeInlineSecondWorkerAlive", secondWorker.isAlive())
                .put("activeInlineNewValue", newValue)
                .put("activeInlineProbeRemoved", removed).put("activeInlineRestored", restored);
        JSONArray precommitDelta = new JSONArray();
        for (int index = 0; index < precommitBefore.length; ++index)
            precommitDelta.put(compiledPrecommitDiagnostics(index) - precommitBefore[index]);
        report.put("activeInlinePrecommitDelta", precommitDelta)
                .put("activeInlineSuspendDeoptHits", aotSuspendDeoptHits(false) - deoptHitsBefore); save(report);
        hierarchyCheck(report, "jit_active_inlined_caller_old_frame_new_body",
                before == 41 && enabled == 1 && probe == 1 && inlineHits > 0
                        && (cacheBefore & 16) != 0 && entered && frameState == 15
                        && secondFrameState == 15
                        && code == 0 && structuralAfter > structuralBefore
                        && inPlaceAfter == inPlaceBefore && (cacheAfter & 16) == 0
                        && !worker.isAlive() && oldError.get() == null
                        && !secondWorker.isAlive() && secondOldError.get() == null
                        && Integer.valueOf(1).equals(oldResult.get())
                        && Integer.valueOf(1).equals(secondOldResult.get()) && newValue == 2
                        && removed == 1 && restored == 1);

        InlineActiveCallee.release.set(false);
        Method evolvedValue = InlineActiveCallee.class.getMethod("value", int.class);
        int secondEnable = jitInlineExperiment(true);
        int secondProbe = secondEnable == 1
                ? configureGraphInlineProbe(caller, evolvedValue, true) : -1;
        int secondForce = -1, secondCacheBefore = -1, secondActiveFrame = -1;
        int secondCode = -1, secondCacheAfter = -1, thirdValue = -1;
        int secondCommitBefore = commitCalls(), secondCommitAfter = -1;
        int secondProbeRemoved = -1, secondRestored;
        long secondGraphHits = -1;
        AtomicReference<Object> evolvedOldResult = new AtomicReference<>();
        AtomicReference<Throwable> evolvedOldError = new AtomicReference<>();
        Thread evolvedWorker = new Thread(() -> {
            try { evolvedOldResult.set(InlineActiveCaller.call(callee, -1)); }
            catch (Throwable error) { evolvedOldError.set(error); }
        }, "jit-active-inline-generation-2");
        try {
            if (secondProbe == 1) {
                secondForce = forceOptimizedJit(caller);
                secondGraphHits = graphInlineHits();
                for (int attempt = 0; attempt < 8 && secondGraphHits == 0; ++attempt) {
                    for (int i = 0; i < 10000; ++i)
                        sink ^= (Integer) caller.invoke(null, callee, 40);
                    invalidateAllJit();
                    secondForce = forceOptimizedJit(caller);
                    secondGraphHits = graphInlineHits();
                }
                secondCacheBefore = jitCacheDetail(caller);
                if (secondGraphHits > 0 && (secondCacheBefore & 16) != 0) {
                    evolvedWorker.start();
                    for (int attempt = 0; attempt < 100; ++attempt) {
                        secondActiveFrame = jitActiveFrameState(evolvedWorker, caller, evolvedValue);
                        if (secondActiveFrame == 15) break;
                        Thread.sleep(10);
                    }
                    report.put("generation2InlineGraphHits", secondGraphHits)
                            .put("generation2ActiveFrameState", secondActiveFrame); save(report);
                    if (secondActiveFrame == 15) {
                        secondCode = redefine(InlineActiveCallee.class, asset(
                                "inline-active-third/com/github/ytlog/artdcevm/fixture/InlineActiveCallee.dex"));
                        secondCommitAfter = commitCalls();
                        secondCacheAfter = jitCacheDetail(caller);
                    }
                }
            }
        } finally {
            InlineActiveCallee.release.set(true);
            if (evolvedWorker.isAlive()) evolvedWorker.join(10000);
            if (secondProbe == 1)
                secondProbeRemoved = configureGraphInlineProbe(caller, evolvedValue, false);
            secondRestored = jitInlineExperiment(false);
        }
        if (secondCode == 0 && !evolvedWorker.isAlive())
            thirdValue = InlineActiveCaller.call(callee, -1);
        report.put("generation2Enable", secondEnable)
                .put("generation2Probe", secondProbe)
                .put("generation2Force", secondForce)
                .put("generation2InlineGraphHits", secondGraphHits)
                .put("generation2CacheBefore", secondCacheBefore)
                .put("generation2ActiveFrameState", secondActiveFrame)
                .put("generation2EvolutionCode", secondCode)
                .put("generation2StructuralBefore", secondCommitBefore)
                .put("generation2StructuralAfter", secondCommitAfter)
                .put("generation2CacheAfter", secondCacheAfter)
                .put("generation2OldResult", String.valueOf(evolvedOldResult.get()))
                .put("generation2OldError", String.valueOf(evolvedOldError.get()))
                .put("generation2WorkerAlive", evolvedWorker.isAlive())
                .put("generation3NewValue", thirdValue)
                .put("generation2ProbeRemoved", secondProbeRemoved)
                .put("generation2Restored", secondRestored); save(report);
        hierarchyCheck(report, "jit_active_inlined_frame_survives_second_generation",
                secondEnable == 1 && secondProbe == 1 && secondGraphHits > 0
                        && (secondCacheBefore & 16) != 0 && secondActiveFrame == 15
                        && secondCode == 0 && secondCommitAfter == secondCommitBefore + 1
                        && (secondCacheAfter & 16) == 0 && !evolvedWorker.isAlive()
                        && evolvedOldError.get() == null
                        && Integer.valueOf(2).equals(evolvedOldResult.get())
                        && thirdValue == 3 && secondProbeRemoved == 1 && secondRestored == 1);
        return true;
    }
    private boolean testProductionNarrowRoundTrip(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_roundtrip"); save(report);
        NarrowSubject subject = new NarrowSubject();
        NarrowChild child = new NarrowChild();
        NarrowParent oldAlias = subject;
        subject.own = 41; child.own = 51;
        hierarchyCheck(report, "narrow_roundtrip_baseline",
                NarrowParentHotCaller.read(oldAlias) == 13
                        && NarrowParentHotCaller.invoke(oldAlias) == 14);
        long activations = receiverGuardTransactionDiagnostics(0);
        long retentions = receiverGuardTransactionDiagnostics(2);
        int first = redefine(NarrowSubject.class, asset(
                "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        report.put("narrowRoundTripFirst", first); save(report);
        boolean firstRejected = false;
        try { NarrowParentHotCaller.read(oldAlias); }
        catch (IncompatibleClassChangeError expected) { firstRejected = true; }
        hierarchyCheck(report, "narrow_roundtrip_first_removal",
                first == 0 && firstRejected && allMethodsDeoptimized()
                        && NarrowSubject.class.getSuperclass() == Object.class
                        && subject.own == 41 && child.own == 51
                        && NarrowHotCaller.call(subject) == 43
                        && NarrowHotCaller.call(child) == 53);
        int restore = redefine(NarrowSubject.class, asset(
                "hier-narrow-restore/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        report.put("narrowRoundTripRestore", restore); save(report);
        hierarchyCheck(report, "narrow_roundtrip_restore_parent",
                restore == 0 && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && NarrowParent.class.isInstance(subject) && NarrowParent.class.isInstance(child)
                        && NarrowParentHotCaller.read(oldAlias) == 0
                        && NarrowParentHotCaller.invoke(oldAlias) == 1
                        && subject.own == 41 && child.own == 51);
        NarrowSubject fresh = new NarrowSubject();
        hierarchyCheck(report, "narrow_roundtrip_fresh_parent_initialized",
                NarrowParentHotCaller.read(fresh) == 13
                        && NarrowParentHotCaller.invoke(fresh) == 14);
        int second = redefine(NarrowSubject.class, asset(
                "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        report.put("narrowRoundTripSecond", second); save(report);
        boolean secondRejected = false, freshRejected = false;
        try { NarrowParentHotCaller.read(oldAlias); }
        catch (IncompatibleClassChangeError expected) { secondRejected = true; }
        try { NarrowParentHotCaller.read(fresh); }
        catch (IncompatibleClassChangeError expected) { freshRejected = true; }
        hierarchyCheck(report, "narrow_roundtrip_second_removal",
                second == 0 && secondRejected && freshRejected
                        && NarrowSubject.class.getSuperclass() == Object.class
                        && subject.own == 41 && child.own == 51 && fresh.own == 17
                        && NarrowHotCaller.call(subject) == 43
                        && NarrowHotCaller.call(child) == 53);
        hierarchyCheck(report, "narrow_roundtrip_guard_retained",
                allMethodsDeoptimized() && !fieldReadProbeMode(false, false)
                        && receiverGuardTransactionDiagnostics(0) == activations + 1
                        && receiverGuardTransactionDiagnostics(2) == retentions + 2
                        && invokeGuardDiagnostics(2) == 0);
        return true;
    }
    private boolean testProductionNarrowConcurrent(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_concurrent"); save(report);
        NarrowSubject subject = new NarrowSubject();
        NarrowParent alias = subject;
        Method readMethod = NarrowParentHotCaller.class.getMethod("read", NarrowParent.class);
        Method invokeMethod = NarrowParentHotCaller.class.getMethod("invoke", NarrowParent.class);
        int readJit = 0, invokeJit = 0;
        for (int attempt = 0; attempt < 20 && (readJit != 2 || invokeJit != 2); ++attempt) {
            for (int i = 0; i < 100000; ++i) {
                sink ^= NarrowParentHotCaller.read(alias);
                sink ^= NarrowParentHotCaller.invoke(alias);
            }
            Thread.sleep(20);
            readJit = jitMethodState(readMethod); invokeJit = jitMethodState(invokeMethod);
        }
        report.put("narrowConcurrentReadJitBefore", readJit)
                .put("narrowConcurrentInvokeJitBefore", invokeJit); save(report);
        hierarchyCheck(report, "narrow_concurrent_jit_ready", readJit == 2 && invokeJit == 2);
        AtomicInteger phase = new AtomicInteger(0);
        AtomicInteger readBaseline = new AtomicInteger(), invokeBaseline = new AtomicInteger();
        AtomicInteger readRejected = new AtomicInteger(), invokeRejected = new AtomicInteger();
        AtomicInteger wrong = new AtomicInteger(), unexpected = new AtomicInteger();
        Thread reader = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    int value = NarrowParentHotCaller.read(alias);
                    if (state == 1 || value != 13) wrong.incrementAndGet();
                    else readBaseline.incrementAndGet();
                } catch (IncompatibleClassChangeError expected) {
                    if (state == 1) readRejected.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-old-field-reader");
        Thread invoker = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    int value = NarrowParentHotCaller.invoke(alias);
                    if (state == 1 || value != 14) wrong.incrementAndGet();
                    else invokeBaseline.incrementAndGet();
                } catch (IncompatibleClassChangeError expected) {
                    if (state == 1) invokeRejected.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-old-method-invoker");
        reader.start(); invoker.start();
        int code;
        try {
            long baselineDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((readBaseline.get() < 100 || invokeBaseline.get() < 100)
                    && System.nanoTime() < baselineDeadline) Thread.sleep(10);
            report.put("narrowConcurrentBaselineRead", readBaseline.get())
                    .put("narrowConcurrentBaselineInvoke", invokeBaseline.get()); save(report);
            if (readBaseline.get() < 100 || invokeBaseline.get() < 100)
                throw new AssertionError("Concurrent baseline did not run");
            code = redefine(NarrowSubject.class, asset(
                    "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
            phase.set(1);
            report.put("narrowConcurrentCommitCode", code)
                    .put("narrowConcurrentDeoptimized", allMethodsDeoptimized()); save(report);
            long rejectionDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((readRejected.get() < 100 || invokeRejected.get() < 100)
                    && System.nanoTime() < rejectionDeadline) Thread.sleep(10);
        } finally {
            phase.set(2);
            reader.join(3000); invoker.join(3000);
        }
        report.put("narrowConcurrentReadRejected", readRejected.get())
                .put("narrowConcurrentInvokeRejected", invokeRejected.get())
                .put("narrowConcurrentWrong", wrong.get())
                .put("narrowConcurrentUnexpected", unexpected.get())
                .put("narrowConcurrentReaderStopped", !reader.isAlive())
                .put("narrowConcurrentInvokerStopped", !invoker.isAlive())
                .put("narrowConcurrentFrameMisses", invokeGuardDiagnostics(2)); save(report);
        hierarchyCheck(report, "narrow_concurrent_commit_and_rejection",
                code == 0 && allMethodsDeoptimized()
                        && readRejected.get() >= 100 && invokeRejected.get() >= 100
                        && wrong.get() == 0 && unexpected.get() == 0
                        && !reader.isAlive() && !invoker.isAlive()
                        && invokeGuardDiagnostics(2) == 0 && subject.own == 17);
        return true;
    }
    private boolean testProductionNarrowSwapConcurrent(JSONObject report, boolean reverse) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_swap_concurrent")
                .put("narrowSwapConcurrentReverse", reverse); save(report);
        NarrowAlternate parent = new NarrowAlternate();
        NarrowSubject subject = new NarrowSubject();
        NarrowParent alias = subject;
        Method oldCaller = NarrowParentHotCaller.class.getMethod("invoke", NarrowParent.class);
        Method newCaller = NarrowHotCaller.class.getMethod("call", NarrowSubject.class);
        int oldJit = 0, newJit = 0;
        for (int attempt = 0; attempt < 20 && (oldJit != 2 || newJit != 2); ++attempt) {
            for (int i = 0; i < 100000; ++i) {
                sink ^= NarrowParentHotCaller.invoke(alias);
                sink ^= NarrowHotCaller.call(subject);
            }
            Thread.sleep(20);
            oldJit = jitMethodState(oldCaller);
            newJit = jitMethodState(newCaller);
        }
        report.put("narrowSwapConcurrentOldJitBefore", oldJit)
                .put("narrowSwapConcurrentNewJitBefore", newJit); save(report);
        hierarchyCheck(report, "narrow_swap_concurrent_jit_ready",
                oldJit == 2 && newJit == 2 && parent.alternate == 29);
        AtomicInteger phase = new AtomicInteger(0);
        AtomicInteger oldBaseline = new AtomicInteger(), newBaseline = new AtomicInteger();
        AtomicInteger oldRejected = new AtomicInteger(), newValues = new AtomicInteger();
        AtomicInteger wrong = new AtomicInteger(), unexpected = new AtomicInteger();
        Thread oldWorker = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    int value = NarrowParentHotCaller.invoke(alias);
                    if (value != 14 || state == 1) wrong.incrementAndGet();
                    else oldBaseline.incrementAndGet();
                } catch (IncompatibleClassChangeError expected) {
                    if (state == 1) oldRejected.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-swap-old-caller");
        Thread newWorker = new Thread(() -> {
            while (phase.get() != 2) {
                int state = phase.get();
                try {
                    int value = NarrowHotCaller.call(subject);
                    if (state == 1) {
                        if (value == 21) newValues.incrementAndGet();
                        else wrong.incrementAndGet();
                    } else if (value == 18) newBaseline.incrementAndGet();
                    else if (value != 21) wrong.incrementAndGet();
                } catch (Throwable error) { unexpected.incrementAndGet(); }
            }
        }, "dcevm-swap-new-caller");
        oldWorker.start(); newWorker.start();
        int code;
        try {
            long baselineDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((oldBaseline.get() < 100 || newBaseline.get() < 100)
                    && System.nanoTime() < baselineDeadline) Thread.sleep(10);
            report.put("narrowSwapConcurrentOldBaseline", oldBaseline.get())
                    .put("narrowSwapConcurrentNewBaseline", newBaseline.get()); save(report);
            if (oldBaseline.get() < 100 || newBaseline.get() < 100)
                throw new AssertionError("Swap concurrent baseline did not run");
            byte[] parentDex = asset("hier-narrow-swap-batch/com/github/ytlog/artdcevm/fixture/NarrowAlternate.dex");
            byte[] subjectDex = asset("hier-narrow-swap-batch/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex");
            code = reverse
                    ? redefineBatch(new Class<?>[]{NarrowSubject.class, NarrowAlternate.class},
                            new byte[][]{subjectDex, parentDex})
                    : redefineBatch(new Class<?>[]{NarrowAlternate.class, NarrowSubject.class},
                            new byte[][]{parentDex, subjectDex});
            phase.set(1);
            report.put("narrowSwapConcurrentCommitCode", code)
                    .put("narrowSwapConcurrentDeoptimized", allMethodsDeoptimized()); save(report);
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
            while ((oldRejected.get() < 100 || newValues.get() < 100)
                    && System.nanoTime() < deadline) Thread.sleep(10);
        } finally {
            phase.set(2);
            oldWorker.join(3000);
            newWorker.join(3000);
        }
        report.put("narrowSwapConcurrentOldRejected", oldRejected.get())
                .put("narrowSwapConcurrentNewValues", newValues.get())
                .put("narrowSwapConcurrentWrong", wrong.get())
                .put("narrowSwapConcurrentUnexpected", unexpected.get())
                .put("narrowSwapConcurrentWorkersStopped",
                        !oldWorker.isAlive() && !newWorker.isAlive())
                .put("narrowSwapConcurrentFrameMisses", invokeGuardDiagnostics(2)); save(report);
        hierarchyCheck(report, "narrow_swap_concurrent_commit_and_dispatch",
                code == 0 && allMethodsDeoptimized()
                        && oldRejected.get() >= 100 && newValues.get() >= 100
                        && wrong.get() == 0 && unexpected.get() == 0
                        && !oldWorker.isAlive() && !newWorker.isAlive()
                        && invokeGuardDiagnostics(2) == 0
                        && NarrowAlternate.class.equals(NarrowSubject.class.getSuperclass())
                        && subject.own == 17 && NarrowHotCaller.call(subject) == 21);
        return true;
    }
    private boolean testJitParentSwapSpin(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "jit_parent_swap_spin"); save(report);
        NarrowAlternate newParent = new NarrowAlternate();
        NarrowSubject subject = new NarrowSubject();
        NarrowParent oldAlias = subject;
        Method hot = NarrowParentHotCaller.class.getMethod("invokeAfterGate", NarrowParent.class);
        NarrowParentHotCaller.spinRelease = true;
        int jit = 0;
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i)
                sink ^= NarrowParentHotCaller.invokeAfterGate(oldAlias);
            Thread.sleep(20);
            jit = jitMethodState(hot);
        }
        NarrowParentHotCaller.spinEntered = new CountDownLatch(1);
        NarrowParentHotCaller.spinRelease = false;
        AtomicReference<Integer> oldValue = new AtomicReference<>();
        AtomicReference<Throwable> oldError = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try { oldValue.set(NarrowParentHotCaller.invokeAfterGate(oldAlias)); }
            catch (Throwable error) { oldError.set(error); }
        }, "jit-old-parent-spin");
        int frame = -1, code = -1;
        boolean entered = false;
        long beforeDeoptHits = aotSuspendDeoptHits(false);
        try {
            worker.start();
            entered = NarrowParentHotCaller.spinEntered.await(10, TimeUnit.SECONDS);
            if (entered) frame = jitActiveFrameState(worker, hot, hot);
            report.put("jitParentSpinCompiled", jit).put("jitParentSpinEntered", entered)
                    .put("jitParentSpinFrame", frame); save(report);
            if (jit == 2 && entered) code = redefine(NarrowSubject.class,
                    asset("hier-narrow-swap/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        } finally {
            NarrowParentHotCaller.spinRelease = true;
            worker.join(10000);
        }
        int retryCode = code == 66 && !worker.isAlive()
                ? redefine(NarrowSubject.class,
                        asset("hier-narrow-swap/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex")) : -1;
        long deoptHits = aotSuspendDeoptHits(false) - beforeDeoptHits;
        Throwable laterError = null;
        try { NarrowParentHotCaller.invokeAfterGate(oldAlias); }
        catch (Throwable error) { laterError = error; }
        report.put("jitParentSpinCode", code).put("jitParentSpinRetryCode", retryCode)
                .put("jitParentSpinSuspendDeoptHits", deoptHits)
                .put("jitParentSpinOldValue", String.valueOf(oldValue.get()))
                .put("jitParentSpinOldError", String.valueOf(oldError.get()))
                .put("jitParentSpinLaterError", String.valueOf(laterError))
                .put("jitParentSpinWorkerAlive", worker.isAlive()); save(report);
        hierarchyCheck(report, "jit_active_parent_swap_no_return_point",
                newParent.alternate == 29 && jit == 2 && entered && (frame & 3) == 3
                        && !worker.isAlive()
                        && code == 0 && retryCode == -1 && deoptHits > 0
                        && oldValue.get() == null
                        && oldError.get() instanceof IncompatibleClassChangeError
                        && laterError instanceof IncompatibleClassChangeError
                        && NarrowAlternate.class.equals(NarrowSubject.class.getSuperclass()));
        return true;
    }
    private boolean testProductionNarrowSwapBatch(JSONObject report, boolean reverse) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_swap_batch")
                .put("narrowSwapBatchReverse", reverse); save(report);
        NarrowAlternate parent = new NarrowAlternate();
        parent.alternate = 61;
        NarrowSubject subject = new NarrowSubject();
        NarrowChild child = new NarrowChild();
        NarrowParent oldAlias = subject;
        subject.own = 41;
        child.own = 51;
        MethodHandles.Lookup lookup = MethodHandles.privateLookupIn(
                NarrowSubject.class, MethodHandles.lookup());
        MethodHandle oldSpecial = lookup.findSpecial(NarrowParent.class, "inherited",
                MethodType.methodType(int.class), NarrowSubject.class);
        Method hot = NarrowParentHotCaller.class.getMethod("invoke", NarrowParent.class);
        int jit = 0;
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= NarrowParentHotCaller.invoke(oldAlias);
            Thread.sleep(20);
            jit = jitMethodState(hot);
        }
        report.put("narrowSwapBatchJitBefore", jit); save(report);
        hierarchyCheck(report, "narrow_swap_batch_baseline",
                jit == 2 && parent.alternate == 61 && subject.own == 41
                        && child.own == 51 && NarrowParentHotCaller.invoke(oldAlias) == 14);
        byte[] parentDex = asset("hier-narrow-swap-batch/com/github/ytlog/artdcevm/fixture/NarrowAlternate.dex");
        byte[] subjectDex = asset("hier-narrow-swap-batch/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex");
        int beforeInvalid = commitCalls();
        long activations = receiverGuardTransactionDiagnostics(0);
        long rollbacks = receiverGuardTransactionDiagnostics(1);
        long retentions = receiverGuardTransactionDiagnostics(2);
        boolean deoptimizedBefore = allMethodsDeoptimized();
        int invalidParent = redefineBatch(
                new Class<?>[]{NarrowSubject.class, NarrowAlternate.class},
                new byte[][]{subjectDex, new byte[]{1, 2, 3}});
        boolean parentUnchanged;
        try { NarrowAlternate.class.getField("revision"); parentUnchanged = false; }
        catch (NoSuchFieldException expected) { parentUnchanged = true; }
        report.put("narrowSwapBatchInvalidParentCode", invalidParent)
                .put("narrowSwapBatchInvalidParentCommitDelta", commitCalls() - beforeInvalid)
                .put("narrowSwapBatchInvalidParentActivationDelta",
                        receiverGuardTransactionDiagnostics(0) - activations)
                .put("narrowSwapBatchInvalidParentRollbackDelta",
                        receiverGuardTransactionDiagnostics(1) - rollbacks); save(report);
        hierarchyCheck(report, "narrow_swap_batch_invalid_parent_atomic",
                invalidParent != 0 && commitCalls() == beforeInvalid
                        && receiverGuardTransactionDiagnostics(0) == activations + 1
                        && receiverGuardTransactionDiagnostics(1) == rollbacks + 1
                        && receiverGuardTransactionDiagnostics(2) == retentions
                        && allMethodsDeoptimized() == deoptimizedBefore
                        && parentUnchanged && parent.alternate == 61
                        && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && NarrowParentHotCaller.invoke(oldAlias) == 14
                        && subject.own == 41 && child.own == 51);
        int invalidSubject = redefineBatch(
                new Class<?>[]{NarrowAlternate.class, NarrowSubject.class},
                new byte[][]{parentDex, new byte[]{1, 2, 3}});
        try { NarrowAlternate.class.getField("revision"); parentUnchanged = false; }
        catch (NoSuchFieldException expected) { parentUnchanged = true; }
        report.put("narrowSwapBatchInvalidSubjectCode", invalidSubject)
                .put("narrowSwapBatchInvalidSubjectCommitDelta", commitCalls() - beforeInvalid); save(report);
        hierarchyCheck(report, "narrow_swap_batch_invalid_subject_atomic",
                invalidSubject != 0 && commitCalls() == beforeInvalid
                        && allMethodsDeoptimized() == deoptimizedBefore
                        && parentUnchanged && parent.alternate == 61
                        && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && NarrowParentHotCaller.invoke(oldAlias) == 14
                        && subject.own == 41 && child.own == 51);
        int commits = commitCalls();
        long dependencies = hierarchyDiagnostics(3);
        int code = reverse
                ? redefineBatch(new Class<?>[]{NarrowSubject.class, NarrowAlternate.class},
                        new byte[][]{subjectDex, parentDex})
                : redefineBatch(new Class<?>[]{NarrowAlternate.class, NarrowSubject.class},
                        new byte[][]{parentDex, subjectDex});
        String oldCallOutcome, oldSpecialOutcome;
        try { oldCallOutcome = "returned:" + NarrowParentHotCaller.invoke(oldAlias); }
        catch (Throwable error) { oldCallOutcome = "threw:" + error.getClass().getName(); }
        try { oldSpecialOutcome = "returned:" + oldSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { oldSpecialOutcome = "threw:" + error.getClass().getName(); }
        report.put("narrowSwapBatchCode", code)
                .put("narrowSwapBatchCommitDelta", commitCalls() - commits)
                .put("narrowSwapBatchDependencyDelta", hierarchyDiagnostics(3) - dependencies)
                .put("narrowSwapBatchOldCallOutcome", oldCallOutcome)
                .put("narrowSwapBatchOldSpecialOutcome", oldSpecialOutcome); save(report);
        hierarchyCheck(report, "narrow_swap_batch_committed",
                code == 0 && commitCalls() == commits + 2 && allMethodsDeoptimized()
                        && hierarchyDiagnostics(3) > dependencies
                        && NarrowAlternate.class.equals(NarrowSubject.class.getSuperclass())
                        && NarrowAlternate.class.isInstance(subject)
                        && NarrowAlternate.class.isInstance(child)
                        && !NarrowParent.class.isInstance(subject)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(oldCallOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(oldSpecialOutcome));
        MethodHandle newSpecial = lookup.findSpecial(NarrowAlternate.class, "inherited",
                MethodType.methodType(int.class), NarrowSubject.class);
        int newSpecialValue;
        try { newSpecialValue = (int) newSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Batched new parent special", error); }
        NarrowSubject fresh = new NarrowSubject();
        NarrowAlternate freshParent = new NarrowAlternate();
        int parentRevision = NarrowAlternate.class.getField("revision").getInt(parent);
        int subjectRevision = NarrowAlternate.class.getField("revision").getInt(subject);
        int childRevision = NarrowAlternate.class.getField("revision").getInt(child);
        int freshRevision = NarrowAlternate.class.getField("revision").getInt(fresh);
        report.put("narrowSwapBatchParentRevision", parentRevision)
                .put("narrowSwapBatchSubjectRevision", subjectRevision)
                .put("narrowSwapBatchChildRevision", childRevision)
                .put("narrowSwapBatchFreshRevision", freshRevision)
                .put("narrowSwapBatchNewSpecialValue", newSpecialValue); save(report);
        hierarchyCheck(report, "narrow_swap_batch_state_and_dispatch",
                parent.alternate == 61 && parentRevision == 0 && parent.inherited() == 71
                        && subject.own == 41 && subjectRevision == 0 && subject.compute() == 45
                        && child.own == 51 && childRevision == 0 && child.compute() == 55
                        && newSpecialValue == 10
                        && fresh.own == 17 && freshRevision == 5 && fresh.compute() == 26
                        && freshParent.alternate == 29 && freshParent.inherited() == 44);
        int restored = redefine(NarrowSubject.class, asset(
                "hier-narrow-restore/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        String newSpecialRestored;
        int oldSpecialRestored;
        try { newSpecialRestored = "returned:" + newSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { newSpecialRestored = "threw:" + error.getClass().getName(); }
        try { oldSpecialRestored = (int) oldSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Batched old parent restored", error); }
        report.put("narrowSwapBatchRestoreCode", restored)
                .put("narrowSwapBatchNewSpecialRestored", newSpecialRestored)
                .put("narrowSwapBatchOldSpecialRestored", oldSpecialRestored); save(report);
        hierarchyCheck(report, "narrow_swap_batch_restored",
                restored == 0 && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && !NarrowAlternate.class.isInstance(subject)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(newSpecialRestored)
                        && oldSpecialRestored == 1 && subject.own == 41 && child.own == 51
                        && parent.alternate == 61 && parentRevision == 0);
        return true;
    }
    private boolean testProductionNarrowSwap(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_swap"); save(report);
        NarrowAlternate initializedParent = new NarrowAlternate();
        NarrowSubject subject = new NarrowSubject();
        NarrowChild child = new NarrowChild();
        NarrowParent oldAlias = subject;
        subject.own = 41;
        child.own = 51;
        MethodHandles.Lookup lookup = MethodHandles.privateLookupIn(
                NarrowSubject.class, MethodHandles.lookup());
        MethodHandle oldSpecial = lookup.findSpecial(NarrowParent.class, "inherited",
                MethodType.methodType(int.class), NarrowSubject.class);
        int oldSpecialBaseline;
        try { oldSpecialBaseline = (int) oldSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Old parent special baseline", error); }
        hierarchyCheck(report, "narrow_swap_baseline",
                initializedParent.alternate == 29 && oldSpecialBaseline == 14
                        && NarrowParentHotCaller.invoke(oldAlias) == 14
                        && NarrowSubject.class.getSuperclass() == NarrowParent.class);
        Method oldHot = NarrowParentHotCaller.class.getMethod("invoke", NarrowParent.class);
        int jit = 0;
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= NarrowParentHotCaller.invoke(oldAlias);
            Thread.sleep(20);
            jit = jitMethodState(oldHot);
        }
        report.put("narrowSwapOldCallerJitBefore", jit); save(report);
        hierarchyCheck(report, "narrow_swap_old_caller_compiled", jit == 2);
        int first = redefine(NarrowSubject.class, asset(
                "hier-narrow-swap/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        String oldSpecialOutcome, oldCallOutcome;
        try { oldSpecialOutcome = "returned:" + oldSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { oldSpecialOutcome = "threw:" + error.getClass().getName(); }
        try { oldCallOutcome = "returned:" + NarrowParentHotCaller.invoke(oldAlias); }
        catch (Throwable error) { oldCallOutcome = "threw:" + error.getClass().getName(); }
        report.put("narrowSwapCode", first).put("narrowSwapOldSpecialOutcome", oldSpecialOutcome)
                .put("narrowSwapOldCallOutcome", oldCallOutcome); save(report);
        hierarchyCheck(report, "narrow_swap_parent_changed",
                first == 0 && allMethodsDeoptimized()
                        && NarrowAlternate.class.equals(NarrowSubject.class.getSuperclass())
                        && NarrowAlternate.class.isInstance(subject)
                        && NarrowAlternate.class.isInstance(child)
                        && !NarrowParent.class.isInstance(subject)
                        && !NarrowParent.class.isInstance(child)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(oldSpecialOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(oldCallOutcome)
                        && subject.own == 41 && child.own == 51
                        && subject.compute() == 44 && child.compute() == 54);
        MethodHandle newSpecial = lookup.findSpecial(NarrowAlternate.class, "inherited",
                MethodType.methodType(int.class), NarrowSubject.class);
        int newSpecialValue;
        try { newSpecialValue = (int) newSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("New parent special", error); }
        NarrowSubject fresh = new NarrowSubject();
        int oldAlternateField = NarrowAlternate.class.getField("alternate").getInt(subject);
        int childAlternateField = NarrowAlternate.class.getField("alternate").getInt(child);
        int freshAlternateField = NarrowAlternate.class.getField("alternate").getInt(fresh);
        report.put("narrowSwapNewSpecialValue", newSpecialValue)
                .put("narrowSwapOldAlternateField", oldAlternateField)
                .put("narrowSwapChildAlternateField", childAlternateField)
                .put("narrowSwapFreshAlternateField", freshAlternateField); save(report);
        hierarchyCheck(report, "narrow_swap_new_parent_dispatch",
                newSpecialValue == 10 && oldAlternateField == 0
                        && childAlternateField == 0 && freshAlternateField == 29
                        && ((NarrowAlternate) (Object) subject).inherited() == 10
                        && ((NarrowAlternate) (Object) child).inherited() == 10
                        && ((NarrowAlternate) (Object) fresh).inherited() == 39);
        int restore = redefine(NarrowSubject.class, asset(
                "hier-narrow-restore/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        int oldSpecialRestored;
        String staleNewSpecialOutcome;
        try { oldSpecialRestored = (int) oldSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { throw new AssertionError("Old parent special restored", error); }
        try { staleNewSpecialOutcome = "returned:" + newSpecial.invokeWithArguments(subject); }
        catch (Throwable error) { staleNewSpecialOutcome = "threw:" + error.getClass().getName(); }
        report.put("narrowSwapRestoreCode", restore)
                .put("narrowSwapOldSpecialRestored", oldSpecialRestored)
                .put("narrowSwapNewSpecialAfterRestore", staleNewSpecialOutcome); save(report);
        hierarchyCheck(report, "narrow_swap_restored",
                restore == 0 && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && NarrowParent.class.isInstance(subject)
                        && !NarrowAlternate.class.isInstance(subject)
                        && oldSpecialRestored == 1
                        && "threw:java.lang.IncompatibleClassChangeError".equals(staleNewSpecialOutcome)
                        && subject.own == 41 && child.own == 51 && fresh.own == 17);
        return true;
    }
    private boolean testProductionNarrowSpecial(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "production_narrow_special"); save(report);
        NarrowSubject subject = new NarrowSubject();
        MethodHandles.Lookup lookup = MethodHandles.privateLookupIn(
                NarrowSubject.class, MethodHandles.lookup());
        MethodHandle special = lookup.findSpecial(NarrowParent.class, "inherited",
                MethodType.methodType(int.class), NarrowSubject.class);
        MethodHandle unreflected = lookup.unreflectSpecial(
                NarrowParent.class.getMethod("inherited"), NarrowSubject.class);
        MethodHandle boundSpecial = special.bindTo(subject);
        NarrowChild child = new NarrowChild();
        MethodHandles.Lookup childLookup = MethodHandles.privateLookupIn(
                NarrowChild.class, MethodHandles.lookup());
        MethodHandle inheritedSpecial = childLookup.findSpecial(NarrowParent.class, "inherited",
                MethodType.methodType(int.class), NarrowChild.class);
        MethodHandle inheritedUnreflected = childLookup.unreflectSpecial(
                NarrowParent.class.getMethod("inherited"), NarrowChild.class);
        int baseline, unreflectedBaseline, boundBaseline;
        int inheritedBaseline, inheritedUnreflectedBaseline;
        try {
            baseline = (int) special.invokeWithArguments(subject);
            unreflectedBaseline = (int) unreflected.invokeWithArguments(subject);
            boundBaseline = NarrowBoundHandleCaller.callExact(boundSpecial);
            inheritedBaseline = (int) inheritedSpecial.invokeWithArguments(child);
            inheritedUnreflectedBaseline = (int) inheritedUnreflected.invokeWithArguments(child);
        } catch (Throwable error) {
            throw new AssertionError("Superclass special baseline", error);
        }
        hierarchyCheck(report, "narrow_special_baseline",
                baseline == 14 && unreflectedBaseline == 14 && boundBaseline == 14
                        && inheritedBaseline == 14 && inheritedUnreflectedBaseline == 14);
        Method hotCaller = NarrowBoundHandleCaller.class.getMethod("callExact", MethodHandle.class);
        int jit = 0;
        try {
            for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
                for (int i = 0; i < 100000; ++i)
                    sink ^= NarrowBoundHandleCaller.callExact(boundSpecial);
                Thread.sleep(20);
                jit = jitMethodState(hotCaller);
            }
        } catch (Throwable error) {
            throw new AssertionError("Superclass special JIT warmup", error);
        }
        report.put("narrowSpecialHotCallerJitBefore", jit); save(report);
        hierarchyCheck(report, "narrow_special_hot_caller_compiled", jit == 2);
        int first = redefine(NarrowSubject.class, asset(
                "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        String removedOutcome, unreflectedRemovedOutcome, boundRemovedOutcome;
        String inheritedRemovedOutcome, inheritedUnreflectedRemovedOutcome;
        try { removedOutcome = "returned:" + special.invokeWithArguments(subject); }
        catch (Throwable error) { removedOutcome = "threw:" + error.getClass().getName(); }
        try { unreflectedRemovedOutcome = "returned:" + unreflected.invokeWithArguments(subject); }
        catch (Throwable error) { unreflectedRemovedOutcome = "threw:" + error.getClass().getName(); }
        try { boundRemovedOutcome = "returned:" + NarrowBoundHandleCaller.callExact(boundSpecial); }
        catch (Throwable error) { boundRemovedOutcome = "threw:" + error.getClass().getName(); }
        try { inheritedRemovedOutcome = "returned:" + inheritedSpecial.invokeWithArguments(child); }
        catch (Throwable error) { inheritedRemovedOutcome = "threw:" + error.getClass().getName(); }
        try { inheritedUnreflectedRemovedOutcome = "returned:" + inheritedUnreflected.invokeWithArguments(child); }
        catch (Throwable error) { inheritedUnreflectedRemovedOutcome = "threw:" + error.getClass().getName(); }
        report.put("narrowSpecialRemoveCode", first)
                .put("narrowSpecialRemovedOutcome", removedOutcome)
                .put("narrowUnreflectedSpecialRemovedOutcome", unreflectedRemovedOutcome)
                .put("narrowBoundSpecialRemovedOutcome", boundRemovedOutcome)
                .put("narrowInheritedSpecialRemovedOutcome", inheritedRemovedOutcome)
                .put("narrowInheritedUnreflectedSpecialRemovedOutcome", inheritedUnreflectedRemovedOutcome); save(report);
        hierarchyCheck(report, "narrow_special_removed",
                first == 0 && allMethodsDeoptimized()
                        && NarrowSubject.class.getSuperclass() == Object.class
                        && "threw:java.lang.IncompatibleClassChangeError".equals(removedOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(unreflectedRemovedOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(boundRemovedOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(inheritedRemovedOutcome)
                        && "threw:java.lang.IncompatibleClassChangeError".equals(inheritedUnreflectedRemovedOutcome));
        int restored = redefine(NarrowSubject.class, asset(
                "hier-narrow-restore/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        int restoredValue, unreflectedRestoredValue, boundRestoredValue;
        int inheritedRestoredValue, inheritedUnreflectedRestoredValue;
        try {
            restoredValue = (int) special.invokeWithArguments(subject);
            unreflectedRestoredValue = (int) unreflected.invokeWithArguments(subject);
            boundRestoredValue = NarrowBoundHandleCaller.callExact(boundSpecial);
            inheritedRestoredValue = (int) inheritedSpecial.invokeWithArguments(child);
            inheritedUnreflectedRestoredValue = (int) inheritedUnreflected.invokeWithArguments(child);
        } catch (Throwable error) {
            throw new AssertionError("Superclass special restored", error);
        }
        report.put("narrowSpecialRestoreCode", restored)
                .put("narrowSpecialRestoredValue", restoredValue)
                .put("narrowUnreflectedSpecialRestoredValue", unreflectedRestoredValue)
                .put("narrowBoundSpecialRestoredValue", boundRestoredValue)
                .put("narrowInheritedSpecialRestoredValue", inheritedRestoredValue)
                .put("narrowInheritedUnreflectedSpecialRestoredValue", inheritedUnreflectedRestoredValue); save(report);
        hierarchyCheck(report, "narrow_special_restored",
                restored == 0 && NarrowSubject.class.getSuperclass() == NarrowParent.class
                        && restoredValue == 1 && unreflectedRestoredValue == 1
                        && boundRestoredValue == 1
                        && inheritedRestoredValue == 1 && inheritedUnreflectedRestoredValue == 1);
        return true;
    }
    private boolean testProductionNarrowing(JSONObject report) throws Exception {
        report.put("phase", "production_narrow_baseline"); save(report);
        if (!backend.capabilities().getBoolean("hierarchyNarrowing")) return false;
        NarrowSubject subject = new NarrowSubject();
        NarrowParent alias = subject;
        if (NarrowParentHotCaller.read(alias) != 13 || NarrowParentHotCaller.invoke(alias) != 14)
            return false;
        Method hot = NarrowParentHotCaller.class.getMethod("read", NarrowParent.class);
        int jit = 0;
        for (int attempt = 0; attempt < 20 && jit != 2; ++attempt) {
            for (int i = 0; i < 100000; ++i) sink ^= NarrowParentHotCaller.read(alias);
            Thread.sleep(20);
            jit = jitMethodState(hot);
        }
        report.put("productionNarrowJitBefore", jit); save(report);
        long activations = receiverGuardTransactionDiagnostics(0);
        long retention = receiverGuardTransactionDiagnostics(2);
        int before = commitCalls();
        int code = redefine(NarrowSubject.class, asset(
                "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        report.put("productionNarrowCode", code)
                .put("productionNarrowDeoptimized", allMethodsDeoptimized())
                .put("productionNarrowGuardActivations", receiverGuardTransactionDiagnostics(0) - activations)
                .put("productionNarrowGuardRetentions", receiverGuardTransactionDiagnostics(2) - retention);
        save(report);
        if (code != 0) return false;
        boolean fieldRejected = false, methodRejected = false;
        try { NarrowParentHotCaller.read(alias); }
        catch (IncompatibleClassChangeError expected) { fieldRejected = true; }
        try { NarrowParentHotCaller.invoke(alias); }
        catch (IncompatibleClassChangeError expected) { methodRejected = true; }
        report.put("productionNarrowFieldRejected", fieldRejected)
                .put("productionNarrowMethodRejected", methodRejected)
                .put("productionNarrowOwn", subject.own)
                .put("productionNarrowSuperclass", NarrowSubject.class.getSuperclass().getName()); save(report);
        return jit == 2 && commitCalls() == before + 1 && allMethodsDeoptimized()
                && receiverGuardTransactionDiagnostics(0) == activations + 1
                && receiverGuardTransactionDiagnostics(2) == retention + 1
                && fieldRejected && methodRejected && subject.own == 17
                && NarrowSubject.class.getSuperclass() == Object.class;
    }
    private boolean testSimpleHierarchyNarrowing(JSONObject report) throws Exception {
        if (!report.has("hierarchyChecks")) report.put("hierarchyChecks", new JSONArray());
        report.put("phase", "hierarchy_narrow_simple_baseline"); save(report);
        hierarchyCheck(report, "hierarchy_narrow_capability",
                backend.capabilities().getBoolean("hierarchyNarrowing"));
        NarrowSubject subject = new NarrowSubject();
        NarrowChild child = new NarrowChild();
        NarrowParent staleParentAlias = subject;
        NarrowSubject[] typedArray = {subject};
        NarrowParent[] parentArrayAlias = typedArray;
        NarrowParent[] directParentArray = {subject};
        subject.own = 41; child.own = 51;
        Class<?> identity = subject.getClass(), childIdentity = child.getClass();
        Field oldParentField = NarrowParent.class.getField("ancestor");
        MethodHandles.Lookup boundLookup = MethodHandles.lookup();
        MethodHandle boundGetter = boundLookup.findGetter(NarrowParent.class, "ancestor", int.class)
                .bindTo(subject);
        MethodHandle boundMethod = boundLookup.findVirtual(NarrowParent.class, "inherited",
                MethodType.methodType(int.class)).bindTo(subject);
        MethodHandle boundVarGetter = boundLookup.findVarHandle(NarrowParent.class, "ancestor", int.class)
                .toMethodHandle(VarHandle.AccessMode.GET).bindTo(subject);
        Method hot = NarrowHotCaller.class.getMethod("call", NarrowSubject.class);
        Method parentHot = NarrowParentHotCaller.class.getMethod("read", NarrowParent.class);
        hierarchyCheck(report, "hierarchy_narrow_baseline", identity.getSuperclass() == NarrowParent.class
                && NarrowParent.class.isInstance(subject) && NarrowParent.class.isInstance(child)
                && oldParentField.getInt(subject) == 13 && NarrowHotCaller.call(subject) == 42
                && NarrowHotCaller.call(child) == 52
                && NarrowParentHotCaller.invoke(staleParentAlias) == 14
                && NarrowParentHotCaller.read(staleParentAlias) == 13
                && NarrowSuperHotCaller.call(child) == 14
                && NarrowTypeHotCaller.isParent(subject)
                && NarrowTypeHotCaller.isParentArray(typedArray)
                && NarrowTypeHotCaller.castParent(subject) == staleParentAlias
                && NarrowTypeHotCaller.castParentArray(typedArray) == parentArrayAlias
                && NarrowTypeHotCaller.readParentArrayField(directParentArray) == 13
                && NarrowTypeHotCaller.invokeParentArrayMethod(parentArrayAlias) == 14
                && NarrowBoundHandleCaller.callExactChecked(boundGetter) == 13
                && NarrowBoundHandleCaller.callAdaptedChecked(boundMethod) == 14
                && NarrowBoundHandleCaller.callExactChecked(boundVarGetter) == 13);
        for (int i = 0; i < 100000; ++i) {
            sink ^= NarrowHotCaller.call((i & 1) == 0 ? subject : child);
            sink ^= NarrowParentHotCaller.read(subject);
        }
        int jit = jitMethodState(hot);
        int parentJit = jitMethodState(parentHot);
        for (int attempt = 0; attempt < 20 && (jit != 2 || parentJit != 2)
                && !allMethodsDeoptimized(); ++attempt) {
            for (int i = 0; i < 20000; ++i) {
                sink ^= NarrowHotCaller.call(subject);
                sink ^= NarrowParentHotCaller.read(subject);
            }
            Thread.sleep(20);
            jit = jitMethodState(hot);
            parentJit = jitMethodState(parentHot);
        }
        report.put("hierarchyNarrowJitStateBefore", jit).put("hierarchyNarrowDeoptimizedBefore",
                allMethodsDeoptimized()).put("hierarchyNarrowParentJitStateBefore", parentJit); save(report);
        hierarchyCheck(report, "hierarchy_narrow_hot_caller_compiled_or_deoptimized",
                (jit == 2 && parentJit == 2) || allMethodsDeoptimized());
        int before = commitCalls();
        long guardRetentionsBefore = receiverGuardTransactionDiagnostics(2);
        int code = redefine(identity, asset("hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        report.put("hierarchyNarrowSimpleResult", code); save(report);
        hierarchyCheck(report, "hierarchy_narrow_committed_with_new_graph",
                code == 0 && commitCalls() == before + 1 && allMethodsDeoptimized()
                && receiverGuardTransactionDiagnostics(2) == guardRetentionsBefore + 1
                && !fieldReadProbeMode(false, false)
                && subject.getClass() == identity && child.getClass() == childIdentity
                && identity.getSuperclass() == Object.class
                && childIdentity.getSuperclass() == identity
                && !NarrowParent.class.isInstance(subject) && !NarrowParent.class.isInstance(child));
        hierarchyCheck(report, "hierarchy_narrow_own_state_and_dispatch",
                subject.own == 41 && child.own == 51
                && NarrowHotCaller.call(subject) == 43 && NarrowHotCaller.call(child) == 53
                && !NarrowTypeHotCaller.isParent(subject)
                && !NarrowTypeHotCaller.isParentArray(typedArray)
                && directParentArray[0] == subject && parentArrayAlias[0] == subject);
        boolean fieldRejected = false, invokeRejected = false, arrayFieldRejected = false;
        boolean arrayInvokeRejected = false, superRejected = false, reflectionRejected = false;
        boolean boundGetterRejected = false, boundMethodRejected = false, boundVarRejected = false;
        try { NarrowParentHotCaller.read(staleParentAlias); }
        catch (IncompatibleClassChangeError expected) { fieldRejected = true; }
        try { NarrowParentHotCaller.invoke(staleParentAlias); }
        catch (IncompatibleClassChangeError expected) { invokeRejected = true; }
        try { NarrowTypeHotCaller.readParentArrayField(directParentArray); }
        catch (IncompatibleClassChangeError expected) { arrayFieldRejected = true; }
        try { NarrowTypeHotCaller.invokeParentArrayMethod(parentArrayAlias); }
        catch (IncompatibleClassChangeError expected) { arrayInvokeRejected = true; }
        try { NarrowSuperHotCaller.call(child); }
        catch (NoSuchMethodError expected) { superRejected = true; }
        try { oldParentField.getInt(subject); }
        catch (IllegalArgumentException expected) { reflectionRejected = true; }
        try { NarrowBoundHandleCaller.callExactChecked(boundGetter); }
        catch (AssertionError expected) { boundGetterRejected = expected.getCause() instanceof IllegalStateException; }
        try { NarrowBoundHandleCaller.callAdaptedChecked(boundMethod); }
        catch (AssertionError expected) { boundMethodRejected = expected.getCause() instanceof IllegalStateException; }
        try { NarrowBoundHandleCaller.callExactChecked(boundVarGetter); }
        catch (AssertionError expected) { boundVarRejected = expected.getCause() instanceof IllegalStateException; }
        hierarchyCheck(report, "hierarchy_narrow_old_access_rejected",
                fieldRejected && invokeRejected && arrayFieldRejected && arrayInvokeRejected
                        && superRejected && reflectionRejected && boundGetterRejected
                        && boundMethodRejected && boundVarRejected
                        && subject.own == 41 && child.own == 51);
        return true;
    }
    private boolean testEmptyHierarchyNarrowing(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "hierarchy_narrow_empty_baseline");
        save(report);
        Class<?> identity = NarrowSubject.class;
        Class<?> childIdentity = NarrowChild.class;
        hierarchyCheck(report, "hierarchy_narrow_empty_old_graph",
                identity.getSuperclass() == NarrowParent.class
                        && childIdentity.getSuperclass() == identity
                        && NarrowNewParentCaller.touch() == 1);
        int before = commitCalls();
        long guardActivationsBefore = receiverGuardTransactionDiagnostics(0);
        long guardRollbacksBefore = receiverGuardTransactionDiagnostics(1);
        long guardRetentionsBefore = receiverGuardTransactionDiagnostics(2);
        int code = redefine(identity, asset("hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex"));
        long guardActivationsAfter = receiverGuardTransactionDiagnostics(0);
        long guardRollbacksAfter = receiverGuardTransactionDiagnostics(1);
        report.put("hierarchyNarrowEmptyResult", code)
                .put("hierarchyNarrowGuardActivationsBefore", guardActivationsBefore)
                .put("hierarchyNarrowGuardActivationsAfter", guardActivationsAfter)
                .put("hierarchyNarrowGuardRollbacksBefore", guardRollbacksBefore)
                .put("hierarchyNarrowGuardRollbacksAfter", guardRollbacksAfter)
                .put("hierarchyNarrowGuardRetentionsAfter", receiverGuardTransactionDiagnostics(2))
                .put("hierarchyNarrowGuardDeoptimizedAfter", allMethodsDeoptimized()); save(report);
        hierarchyCheck(report, "hierarchy_narrow_guard_precommit_and_retention",
                guardActivationsAfter == guardActivationsBefore + 1
                        && guardRollbacksAfter == guardRollbacksBefore
                        && receiverGuardTransactionDiagnostics(2) == guardRetentionsBefore + 1
                        && allMethodsDeoptimized());
        hierarchyCheck(report, "hierarchy_narrow_empty_committed",
                code == 0 && commitCalls() == before + 1
                        && identity.getSuperclass() == Object.class
                        && childIdentity.getSuperclass() == identity);
        NarrowSubject subject = new NarrowSubject();
        NarrowChild child = new NarrowChild();
        hierarchyCheck(report, "hierarchy_narrow_empty_new_instances_migrated",
                subject.getClass() == identity && child.getClass() == childIdentity
                        && subject.own == 17 && child.own == 17
                        && NarrowHotCaller.call(subject) == 19
                        && NarrowHotCaller.call(child) == 19
                        && !NarrowParent.class.isInstance(subject)
                        && !NarrowParent.class.isInstance(child)
                        && !NarrowTypeHotCaller.isParent(subject)
                        && !NarrowTypeHotCaller.isParentArray(new NarrowSubject[]{subject}));
        boolean staleFieldRejected = false, staleMethodRejected = false;
        try { NarrowNewParentCaller.readFresh(); }
        catch (IncompatibleClassChangeError expected) { staleFieldRejected = true; }
        try { NarrowNewParentCaller.invokeFresh(); }
        catch (IncompatibleClassChangeError expected) { staleMethodRejected = true; }
        report.put("hierarchyNarrowEmptyStaleFieldRejected", staleFieldRejected)
                .put("hierarchyNarrowEmptyStaleMethodRejected", staleMethodRejected); save(report);
        hierarchyCheck(report, "hierarchy_narrow_empty_old_creator_rejected",
                staleFieldRejected && staleMethodRejected && subject.own == 17);
        return true;
    }
    private boolean testArtFieldReadProbe(JSONObject report) throws Exception {
        report.put("phase", "art_field_read_probe"); save(report);
        NarrowParent parent = new NarrowParent();
        NarrowSubject validSubject = new NarrowSubject();
        NarrowParentHotCaller.invoke(parent);
        Method hot = NarrowParentHotCaller.class.getMethod("read", NarrowParent.class);
        Method writeHot = NarrowParentHotCaller.class.getMethod("write", NarrowParent.class, int.class);
        Method invokeHot = NarrowHotCaller.class.getMethod("call", NarrowSubject.class);
        for (int i = 0; i < 100000; ++i) {
            sink ^= NarrowParentHotCaller.read(parent);
            NarrowParentHotCaller.write(parent, 13);
            sink ^= NarrowHotCaller.call(validSubject);
        }
        int jit = jitMethodState(hot);
        int writeJit = jitMethodState(writeHot);
        int invokeJit = jitMethodState(invokeHot);
        for (int attempt = 0; attempt < 20 && (jit != 2 || writeJit != 2 || invokeJit != 2)
                && !allMethodsDeoptimized(); ++attempt) {
            for (int i = 0; i < 20000; ++i) {
                sink ^= NarrowParentHotCaller.read(parent);
                NarrowParentHotCaller.write(parent, 13);
                sink ^= NarrowHotCaller.call(validSubject);
            }
            Thread.sleep(20);
            jit = jitMethodState(hot);
            writeJit = jitMethodState(writeHot);
            invokeJit = jitMethodState(invokeHot);
        }
        report.put("fieldReadJitBefore", jit).put("fieldWriteJitBefore", writeJit)
                .put("invokeCallerJitBefore", invokeJit)
                .put("fieldReadBaseline", NarrowParentHotCaller.read(parent));
        save(report);
        if (jit != 2 || writeJit != 2 || invokeJit != 2
                || NarrowParentHotCaller.read(parent) != 13) return false;
        boolean enabled = fieldReadProbeMode(true, true);
        report.put("fieldReadProbeEnabled", enabled)
                .put("fieldReadDeoptimized", allMethodsDeoptimized()); save(report);
        if (!enabled) return false;
        boolean rejected = false;
        boolean writeRejected = false;
        int validInvoke = 0;
        try {
            validInvoke = NarrowHotCaller.call(validSubject);
            try {
                NarrowParentHotCaller.read(parent);
            } catch (IncompatibleClassChangeError expected) {
                rejected = true;
            }
            try {
                NarrowParentHotCaller.write(parent, 99);
            } catch (IncompatibleClassChangeError expected) {
                writeRejected = true;
            }
        } finally {
            if (!fieldReadProbeMode(false, false)) throw new AssertionError("FieldRead probe cleanup failed");
        }
        report.put("fieldReadRejectedBeforeAccess", rejected)
                .put("fieldWriteRejectedBeforeMutation", writeRejected)
                .put("invokeGuardValidResult", validInvoke)
                .put("fieldReadProbeHits", fieldReadProbeHits())
                .put("invokeGuardChecks", invokeGuardDiagnostics(0))
                .put("invokeGuardRejections", invokeGuardDiagnostics(1))
                .put("invokeGuardFrameMisses", invokeGuardDiagnostics(2))
                .put("fieldReadValueAfterDisable", NarrowParentHotCaller.read(parent)); save(report);
        return rejected && writeRejected && validInvoke == 18
                && invokeGuardDiagnostics(0) > 0 && invokeGuardDiagnostics(1) == 0
                && invokeGuardDiagnostics(2) == 0 && fieldReadProbeHits() >= 2
                && NarrowParentHotCaller.read(parent) == 13;
    }
    private boolean testJniReceiverProbe(JSONObject report) throws Exception {
        report.put("phase", "jni_receiver_probe"); save(report);
        NarrowSubject subject = new NarrowSubject();
        int baselineField = HierarchyProbe.narrowJniRead(subject);
        int baselineMethod = HierarchyProbe.narrowJniInvoke(subject);
        int baselineNonvirtual = HierarchyProbe.narrowJniInvokeNonvirtual(subject);
        report.put("jniReceiverBaselineField", baselineField)
                .put("jniReceiverBaselineMethod", baselineMethod)
                .put("jniReceiverBaselineNonvirtual", baselineNonvirtual); save(report);
        if (baselineField != 13 || baselineMethod != 14 || baselineNonvirtual != 14) return false;
        if (!fieldReadProbeMode(true, false)) return false;
        JSONArray modes = new JSONArray();
        int previousMode = checkJniMode(-1);
        try {
            for (int checked = 0; checked <= 1; checked++) {
                checkJniMode(checked);
                if (checkJniMode(-1) != checked) return false;
                int field = HierarchyProbe.narrowJniRead(subject);
                int method = HierarchyProbe.narrowJniInvoke(subject);
                int nonvirtual = HierarchyProbe.narrowJniInvokeNonvirtual(subject);
                JSONArray forms = new JSONArray();
                for (int form = 0; form < 4; ++form) forms.put(HierarchyProbe.narrowJniInvokeForm(subject, form));
                modes.put(new JSONObject().put("checked", checked)
                        .put("field", field).put("method", method).put("nonvirtual", nonvirtual)
                        .put("forms", forms));
                report.put("jniReceiverModes", modes); save(report);
                if (field != 13 || method != 14 || nonvirtual != 14) return false;
                for (int form = 0; form < 4; ++form) if (forms.getInt(form) != 14) return false;
            }
        } finally {
            checkJniMode(previousMode);
            if (!fieldReadProbeMode(false, false)) throw new AssertionError("JNI receiver probe cleanup failed");
        }
        report.put("jniReceiverInvokeChecks", invokeGuardDiagnostics(0)); save(report);
        return modes.length() == 2;
    }
    private boolean testHierarchyAdditions(JSONObject report) throws Exception {
        report.put("hierarchyChecks", new JSONArray()).put("phase", "hierarchy_prepare"); save(report);
        HierarchySubject fixture = new HierarchySubject();
        HierarchyChild child = new HierarchyChild();
        HierarchyInserted sentinel = new HierarchyInserted();
        HierarchyOuter outerSentinel = new HierarchyOuter();
        fixture.root = 9_000_000_089L; fixture.base = 17; fixture.value = 57;
        child.root = 8_000_000_089L; child.base = 27; child.value = 67;
        Object own = new Object(), inheritedRef = new Object();
        fixture.ownRef = own; fixture.baseRef = inheritedRef; HierarchySubject.counter = 777;
        HierarchySubject alias = fixture;
        Class<?> identity = fixture.getClass(), childIdentity = child.getClass();
        int hash = System.identityHashCode(fixture);
        HierarchySubject[] array = {fixture, child};
        Field value = identity.getField("value"), root = identity.getField("root"), base = identity.getField("base");
        Method compute = identity.getMethod("compute"), inherited = HierarchyBase.class.getMethod("inherited");
        Method activeMethod = identity.getMethod("active");
        MethodHandle computeHandle = MethodHandles.lookup().findVirtual(identity, "compute", MethodType.methodType(int.class));
        MethodHandle inheritedHandle = MethodHandles.lookup().findVirtual(HierarchyBase.class, "inherited", MethodType.methodType(int.class));
        VarHandle ownHandle = MethodHandles.lookup().findVarHandle(identity, "value", int.class);
        HierarchyProbe.retain(fixture);
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(asset("hier-caller/com/github/ytlog/artdcevm/fixture/HierarchyCaller.dex")), getTargetContext().getClassLoader());
        Class<?> caller = loader.loadClass("com.github.ytlog.artdcevm.fixture.HierarchyCaller");
        Method hot = caller.getMethod("hot", HierarchySubject.class), cold = caller.getMethod("cold", HierarchySubject.class);
        Method isInserted = caller.getMethod("inserted", Object.class), isMore = caller.getMethod("more", Object.class);
        for (int i = 0; i < 20000; ++i) {
            sink = (Integer) hot.invoke(null, fixture);
            if ((Boolean) isInserted.invoke(null, fixture) || (Boolean) isMore.invoke(null, fixture)) throw new AssertionError("Hierarchy baseline");
        }
        int before = commitCalls();
        int invalid = redefineBatch(new Class<?>[]{identity, Peer.class},
                new byte[][]{asset("hier-interface/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex"), new byte[]{1, 2, 3}});
        hierarchyCheck(report, "hierarchy_invalid_batch_atomic", invalid != 0 && commitCalls() == before
                && identity.getInterfaces().length == 0 && fixture.compute() == 75);
        long retiredBeforeInvalid = hierarchyDiagnostics(5);
        long capturedBeforeInvalid = hierarchyDiagnostics(6);
        int badCode = redefine(identity, asset("hier-invalid/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex"));
        hierarchyCheck(report, "new_hierarchy_invalid_bytecode_rejected_before_commit", badCode == 62 && commitCalls() == before
                && identity.getSuperclass() == HierarchyBase.class && fixture.compute() == 75
                && Class.forName(identity.getName()) == identity && !HierarchyInserted.class.isInstance(fixture));
        long retiredAfterInvalid = hierarchyDiagnostics(5);
        long capturedAfterInvalid = hierarchyDiagnostics(6);
        report.put("failedHierarchyRetiredDelta", retiredAfterInvalid - retiredBeforeInvalid)
                .put("failedHierarchyCapturedDelta", capturedAfterInvalid - capturedBeforeInvalid); save(report);
        hierarchyCheck(report, "failed_hierarchy_linked_classes_retired_before_dex_release",
                retiredAfterInvalid > retiredBeforeInvalid && capturedAfterInvalid > capturedBeforeInvalid);
        int badBatch = redefineBatch(new Class<?>[]{HierarchyInserted.class, identity}, new byte[][]{
                asset("hier-parent/com/github/ytlog/artdcevm/fixture/HierarchyInserted.dex"),
                asset("hier-invalid/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex")});
        boolean parentUnchanged;
        try { HierarchyInserted.class.getDeclaredField("batchMarker"); parentUnchanged = false; }
        catch (NoSuchFieldException expected) { parentUnchanged = true; }
        hierarchyCheck(report, "dependent_parent_batch_verifier_failure_atomic", badBatch == 62
                && commitCalls() == before && parentUnchanged && identity.getSuperclass() == HierarchyBase.class);
        int cycle = redefineBatch(new Class<?>[]{identity, HierarchyInserted.class}, new byte[][]{
                asset("hier-parent/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex"),
                asset("hier-cycle/com/github/ytlog/artdcevm/fixture/HierarchyInserted.dex")});
        hierarchyCheck(report, "new_parent_dependency_cycle_rejected_atomic", cycle != 0 && commitCalls() == before
                && identity.getSuperclass() == HierarchyBase.class && HierarchyInserted.class.getSuperclass() == HierarchyBase.class
                && fixture.compute() == 75 && sentinel.inherited() == 47);
        String[] variants = {"hier-interface", "hier-parent", "hier-parent2"};
        Class<?>[] parents = {HierarchyBase.class, HierarchyInserted.class, HierarchyOuter.class};
        for (int generation = 0; generation < variants.length; ++generation) {
            String prefix = "hierarchy_g" + generation;
            int constructorCount = HierarchyInserted.constructions;
            DeletionControl.entered = new CountDownLatch(1); DeletionControl.release = new CountDownLatch(1);
            AtomicReference<Long> oldResult = new AtomicReference<>();
            AtomicReference<Throwable> activeError = new AtomicReference<>();
            Thread active = new Thread(() -> {
                synchronized (fixture) {
                    try {
                        oldResult.set((Long) activeMethod.invoke(fixture));
                        if (!Thread.holdsLock(fixture)) throw new AssertionError("Monitor lost during hierarchy migration");
                    } catch (Throwable error) { activeError.set(cause(error)); }
                }
            }, "active-hierarchy-frame");
            active.start();
            int code;
            try {
                if (!DeletionControl.entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("Hierarchy frame did not enter");
                report.put("phase", prefix + "_commit"); save(report);
                byte[] subjectDex = asset(variants[generation] + "/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex");
                if (generation == 1) {
                    code = redefineBatch(new Class<?>[]{identity, HierarchyInserted.class}, new byte[][]{
                            subjectDex, asset("hier-parent/com/github/ytlog/artdcevm/fixture/HierarchyInserted.dex")});
                } else if (generation == 2) {
                    code = redefineBatch(new Class<?>[]{identity, HierarchyOuter.class, HierarchyBase.class}, new byte[][]{
                            subjectDex, asset("hier-parent2/com/github/ytlog/artdcevm/fixture/HierarchyOuter.dex"),
                            asset("hier-parent2/com/github/ytlog/artdcevm/fixture/HierarchyBase.dex")});
                } else code = redefine(identity, subjectDex);
            } finally { DeletionControl.release.countDown(); active.join(10000); DeletionControl.release = null; }
            report.put(prefix + "Result", code).put("hierarchyParentSelections", hierarchyDiagnostics(0))
                    .put("hierarchyVerifierResolutions", hierarchyDiagnostics(1)).put("hierarchyVerifierEntries", hierarchyDiagnostics(2)); save(report);
            hierarchyCheck(report, prefix + "_commit", code == 0);
            hierarchyCheck(report, prefix + "_old_frame_and_monitor", !active.isAlive() && activeError.get() == null
                    && oldResult.get() != null && oldResult.get() == 9_000_000_147L + generation);
            hierarchyCheck(report, prefix + "_class_identity", fixture == alias && fixture.getClass() == identity
                    && child.getClass() == childIdentity && childIdentity.getSuperclass() == identity
                    && identity.getSuperclass() == parents[generation] && System.identityHashCode(fixture) == hash);
            hierarchyCheck(report, prefix + "_fields_by_declaring_class", value.getInt(fixture) == 57
                    && root.getLong(fixture) == 9_000_000_089L && base.getInt(fixture) == 17
                    && ((HierarchyBase) fixture).baseRef == inheritedRef && fixture.ownRef == own
                    && ((HierarchyRoot) child).root == 8_000_000_089L && ((HierarchyBase) child).base == 27
                    && child.child == 8_888_888_888L && HierarchySubject.counter == 777);
            hierarchyCheck(report, prefix + "_constructors_not_replayed", HierarchyInserted.constructions == constructorCount);
            hierarchyCheck(report, prefix + "_type_relations", HierarchyTag.class.isInstance(fixture)
                    && HierarchyTag.class.isAssignableFrom(identity) && HierarchyTag.class.isInstance(child)
                    && (Boolean) isInserted.invoke(null, fixture) == (generation >= 1)
                    && (Boolean) isMore.invoke(null, fixture) == (generation >= 1)
                    && HierarchyInserted[].class.isAssignableFrom(array.getClass()) == (generation >= 1));
            hierarchyCheck(report, prefix + "_interface_dispatch", ((HierarchyTag) (Object) fixture).tag() == 59 + generation
                    && ((HierarchyTag) (Object) fixture).defaultValue() == 11
                    && (generation == 0 || ((HierarchyMore) (Object) fixture).more() == 22));
            int expectedCompute = generation == 2 ? 61 : 76 + generation;
            int expectedInherited = generation == 0 ? 37 : 57;
            hierarchyCheck(report, prefix + "_cached_methods", (Integer) compute.invoke(fixture) == expectedCompute
                    && (Integer) inherited.invoke(fixture) == expectedInherited && fixture.throughSuper() == expectedInherited);
            try {
                hierarchyCheck(report, prefix + "_cached_handles", (int) computeHandle.invokeWithArguments(fixture) == expectedCompute
                        && (int) inheritedHandle.invokeWithArguments(fixture) == expectedInherited && (int) ownHandle.get(fixture) == 57);
            } catch (Throwable error) { throw new AssertionError(prefix + " handles", error); }
            int jniMode = checkJniMode(-1);
            try {
                for (int checked = 0; checked <= 1; ++checked) {
                    checkJniMode(checked); long[] fields = HierarchyProbe.fields();
                    hierarchyCheck(report, prefix + "_jni_" + checked, fields[0] == 9_000_000_089L && fields[1] == 17 && fields[2] == 57
                            && HierarchyProbe.invoke(false) == expectedInherited && HierarchyProbe.invoke(true) == 37
                            && HierarchyProbe.instanceOf(HierarchyInserted.class) == (generation >= 1));
                }
            } finally { checkJniMode(jniMode); }
            hierarchyCheck(report, prefix + "_resolved_and_cold_field_lookup", (Integer) hot.invoke(null, fixture) == (generation == 2 ? 0 : 17)
                    && (Integer) cold.invoke(null, fixture) == (generation == 2 ? 0 : 17));
            if (generation >= 1) hierarchyCheck(report, prefix + "_inserted_fields_default", HierarchyInserted.class.getField("added").getDouble(fixture) == 0
                    && HierarchyInserted.class.getField("addedRef").get(fixture) == null
                    && HierarchyInserted.class.getField("value").getInt(fixture) == 0);
            if (generation >= 1) hierarchyCheck(report, prefix + "_new_parent_from_same_batch",
                    HierarchyInserted.class.getField("batchMarker").getInt(fixture) == 0
                    && HierarchyInserted.class.getField("batchMarker").getInt(sentinel) == 0
                    && (Integer) identity.getMethod("parentBatchValue").invoke(fixture) == 1217
                    && (Integer) identity.getMethod("parentBatchValue").invoke(child) == 1227);
            if (generation == 2) hierarchyCheck(report, prefix + "_common_ancestor_batch_state",
                    HierarchyBase.class.getField("batchRoot").getInt(fixture) == 0
                    && HierarchyBase.class.getField("batchRoot").getInt(sentinel) == 0
                    && HierarchyOuter.class.getField("outerMarker").getInt(fixture) == 0
                    && HierarchyOuter.class.getField("outerMarker").getInt(outerSentinel) == 0);
            if (generation == 2) hierarchyCheck(report, prefix + "_outer_fields_default", HierarchyOuter.class.getField("outer").getLong(fixture) == 0
                    && HierarchyOuter.class.getField("base").getInt(fixture) == 0);
            hierarchyCheck(report, prefix + "_unrelated_instances_untouched", sentinel.added == 2.5 && sentinel.value == 39
                    && outerSentinel.outer == 12_345_678_900L && outerSentinel.base == 91);
            System.gc();
            hierarchyCheck(report, prefix + "_after_gc", value.getInt(fixture) == 57 && fixture.ownRef == own
                    && array[0] == fixture && array[1] == child);
        }
        if (suite.equals("hierarchy")) {
            HierarchyTag staleTag = (HierarchyTag) (Object) fixture;
            HierarchyMore staleMore = (HierarchyMore) (Object) fixture;
            before = commitCalls();
            long guardActivationsBefore = receiverGuardTransactionDiagnostics(0);
            long guardRollbacksBefore = receiverGuardTransactionDiagnostics(1);
            boolean deoptimizedBefore = allMethodsDeoptimized();
            int narrow = redefine(identity, asset("hier-narrow/com/github/ytlog/artdcevm/fixture/HierarchySubject.dex"));
            report.put("complexNarrowingCode", narrow)
                    .put("complexNarrowingGuardActivations", receiverGuardTransactionDiagnostics(0) - guardActivationsBefore)
                    .put("complexNarrowingGuardRollbacks", receiverGuardTransactionDiagnostics(1) - guardRollbacksBefore)
                    .put("complexNarrowingDeoptimizedBefore", deoptimizedBefore)
                    .put("complexNarrowingDeoptimizedAfter", allMethodsDeoptimized()); save(report);
            hierarchyCheck(report, "hierarchy_complex_parent_and_interface_removal_committed",
                    narrow == 0 && commitCalls() == before + 1
                    && identity.getSuperclass() == HierarchyBase.class
                    && !HierarchyMore.class.isInstance(fixture)
                    && !HierarchyTag.class.isInstance(fixture)
                    && receiverGuardTransactionDiagnostics(0) == guardActivationsBefore + 1
                    && receiverGuardTransactionDiagnostics(1) == guardRollbacksBefore
                    && allMethodsDeoptimized());
            boolean oldTagRejected = false, oldMoreRejected = false;
            try { staleTag.tag(); } catch (IncompatibleClassChangeError expected) { oldTagRejected = true; }
            try { staleMore.more(); } catch (IncompatibleClassChangeError expected) { oldMoreRejected = true; }
            report.put("complexOldTagRejected", oldTagRejected)
                    .put("complexOldMoreRejected", oldMoreRejected)
                    .put("complexSubjectValue", fixture.value)
                    .put("complexBaseValue", ((HierarchyBase) fixture).base)
                    .put("complexCompute", fixture.compute())
                    .put("complexInherited", fixture.inherited()); save(report);
            hierarchyCheck(report, "hierarchy_complex_old_interfaces_rejected",
                    oldTagRejected && oldMoreRejected && fixture.value == 57 && fixture.ownRef == own
                    && ((HierarchyBase) fixture).base == 17
                    && fixture.compute() == 79 && fixture.inherited() == 37);
            HierarchySubject fresh = new HierarchySubject();
            hierarchyCheck(report, "new_constructor_uses_narrowed_ancestry", fresh.value == 37
                    && HierarchyBase.class.getField("base").getInt(fresh) == 7
                    && HierarchyRoot.class.getField("root").getLong(fresh) == 9_000_000_007L
                    && !HierarchyTag.class.isInstance(fresh));
        } else {
            HierarchySubject fresh = new HierarchySubject();
            hierarchyCheck(report, "new_constructor_uses_added_ancestry", fresh.value == 37
                    && HierarchyOuter.class.getField("base").getInt(fresh) == 91
                    && HierarchyInserted.class.getField("added").getDouble(fresh) == 2.5
                    && HierarchyInserted.class.getField("batchMarker").getInt(fresh) == 903
                    && HierarchyBase.class.getField("batchRoot").getInt(fresh) == 601
                    && HierarchyOuter.class.getField("outerMarker").getInt(fresh) == 701
                    && HierarchyOuter.class.getField("outer").getLong(fresh) == 12_345_678_900L);
        }
        report.put("hierarchyDependencyAllocations", hierarchyDiagnostics(3)).put("hierarchyBatchDependenciesPassed", true)
                .put("hierarchyAdditionPassed", true).put("ownedStructuralCommitCalls", commitCalls()).put("phase", "finished"); save(report);
        hierarchyCheck(report, "hierarchy_named_allocator_and_verifier_executed", hierarchyDiagnostics(0) >= 2 && hierarchyDiagnostics(1) > 0 && hierarchyDiagnostics(3) > 0);
        return true;
    }
    private byte[] asset(String path) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(path);
             ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int size;
            while ((size = input.read(buffer)) != -1) bytes.write(buffer, 0, size);
            return bytes.toByteArray();
        }
    }
    private void save(JSONObject report) throws Exception {
        File target = new File(getTargetContext().getFilesDir(), "source-engine.json");
        byte[] bytes = report.toString().getBytes(StandardCharsets.UTF_8);
        File temporary = new File(target.getPath() + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(temporary)) { stream.write(bytes); }
        if (!temporary.renameTo(target)) throw new java.io.IOException("Cannot publish test checkpoint");
    }
}
