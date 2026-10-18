package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Debug;
import android.os.SystemClock;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import com.github.ytlog.artdcevm.fixture.RuntimeThreadFixture;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.CopiedChild;
import com.github.ytlog.artdcevm.fixture.InterfaceEvolutionFixture;
import com.github.ytlog.artdcevm.fixture.CopiedDefault;
import com.github.ytlog.artdcevm.fixture.CopiedParent;
import com.github.ytlog.artdcevm.fixture.NestedInterfaceHost;
import com.github.ytlog.artdcevm.fixture.InterfaceBatchFixture;
import com.github.ytlog.artdcevm.fixture.MultiInterfaceFixture;
import com.github.ytlog.artdcevm.fixture.PointerIdFixture;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.zip.Adler32;

/** Repeated failures and successful transactions in one process, with live instances. */
public final class InterfaceAbortInstrumentation extends Instrumentation {
    private static native int armFailure(int stage);
    private static native int discardedClasses();
    private static native int failureHits();
    static native int allocationOrder();
    static native int allocatedTypes();
    private static native int primePointerId(Class<?> klass, int kind);
    private static native int configureCheckJni(boolean enabled);
    static native int validatedInterfaceGroups();
    static int armMappingPlanFailure() { return armFailure(22); }
    static int armDispatchFailure() { return armFailure(21); }
    static int armPointerFailure(int stage) { return armFailure(stage); }
    private void checkDefaultConflictRollback() throws Exception {
        InterfaceEvolutionFixture.Diamond instance = new InterfaceEvolutionFixture.Diamond();
        instance.marker = 101;
        expect("conflict baseline", 21, instance.conflict());
        expect("left baseline", 11, instance.left());
        expect("default conflict rejection", 63, redefine(InterfaceEvolutionFixture.Left.class,
                asset("interface-default-conflict", InterfaceEvolutionFixture.Left.class)));
        String reason = DcevmRuntime.capabilities().getString("lastRejection");
        if (!reason.contains("InterfaceEvolutionFixture$Diamond;: conflict()I")
                || !reason.contains("conflict between unrelated interface defaults"))
            throw new AssertionError("Missing linked default conflict reason");
        ++checks;
        expect("old interface schema", 1, InterfaceEvolutionFixture.Left.class.getDeclaredMethods().length);
        expect("old default after conflict", 21, instance.conflict());
        expect("old left after conflict", 11, instance.left());
        expect("old parent after conflict", 30, instance.base());
        expect("old field after conflict", 101, instance.marker);
        expect("new object after conflict", 21, new InterfaceEvolutionFixture.Diamond().conflict());
        System.gc(); System.runFinalization(); System.gc();
        expect("default after conflict GC", 21, instance.conflict());
        expect("parent transaction after conflict", 0, redefine(InterfaceEvolutionFixture.ConflictBase.class,
                asset("interface-conflict-recovery", InterfaceEvolutionFixture.ConflictBase.class)));
        expect("parent behavior after conflict", 32, instance.base());
        expect("parent addition after conflict", 33, (Integer) InterfaceEvolutionFixture.ConflictBase.class
                .getMethod("added").invoke(instance));
        expect("interface transaction after conflict", 0, redefine(InterfaceEvolutionFixture.Left.class,
                asset("interface-conflict-recovery", InterfaceEvolutionFixture.Left.class)));
        expect("interface behavior after conflict", 13, instance.left());
        expect("other default preserved", 21, instance.conflict());
        expect("field after recovery", 101, instance.marker);
        System.gc(); System.runFinalization(); System.gc();
        expect("interface after recovery GC", 13, instance.left());
        expect("parent after recovery GC", 32, instance.base());
        expect("parent addition after recovery GC", 33, (Integer) InterfaceEvolutionFixture.ConflictBase.class
                .getMethod("added").invoke(instance));
    }

    private static native void rejectRuntimeThreadProbe();
    private static native long runtimeReferenceReads();
    private static native long runtimeMarkingReads();

    private void checkJniIdRuntime(Bundle result, boolean pointerMode) throws Exception {
        PointerIdFixture.Implementation instance = new PointerIdFixture.Implementation();
        StandardJvmtiClient.cacheInterfaceDispatch(0,
                PointerIdFixture.Contract.class.getMethod("pointerValue"));
        expect("JNI ID mode interface admission", pointerMode ? 79 : 0, redefine(PointerIdFixture.Contract.class,
                asset("interface-pointer-body", PointerIdFixture.Contract.class)));
        String reason = DcevmRuntime.capabilities().getString("lastRejection");
        if (pointerMode ? !reason.contains("active pointer JNI IDs") : !reason.isEmpty())
            throw new AssertionError("Unexpected JNI ID mode rejection reason");
        ++checks;
        expect("JNI ID mode interface dispatch", pointerMode ? 51 : 52, instance.pointerValue());
        expect("JNI ID mode cached JNI dispatch", pointerMode ? 51 : 52,
                StandardJvmtiClient.callInterfaceDispatchInt(instance, 0));
        RuntimeThreadFixture ordinary = new RuntimeThreadFixture();
        ordinary.kept = 137;
        expect("pointer runtime preserves class admission", 0, redefine(RuntimeThreadFixture.class,
                asset("runtime-thread-one", RuntimeThreadFixture.class)));
        expect("pointer runtime class body", 12, ordinary.value());
        expect("pointer runtime class field", 137, ordinary.kept);
        System.gc(); System.runFinalization(); System.gc();
        expect("JNI ID mode cached JNI after GC", pointerMode ? 51 : 52,
                StandardJvmtiClient.callInterfaceDispatchInt(instance, 0));
        expect("JNI ID mode field-only interface admission", pointerMode ? 79 : 0,
                redefine(PointerIdFixture.FieldsOnly.class,
                        asset("interface-pointer-body", PointerIdFixture.FieldsOnly.class)));
        expect("field-only interface method count", pointerMode ? 0 : 1,
                PointerIdFixture.FieldsOnly.class.getDeclaredMethods().length);
        expect("field-only interface preserves field", 53,
                PointerIdFixture.FieldsOnly.class.getField("VALUE").getInt(null));
        if (!pointerMode) expect("field-only interface addition", 54,
                (Integer) PointerIdFixture.FieldsOnly.class.getMethod("added").invoke(null));
        result.putInt("checks", checks);
        result.putBoolean("completed", true);
    }

    // Runs before any interface transaction: the ordinary-class path must
    // provide its own Thread probe and barrier, including after a rejected probe.
    private void checkRuntimeThread(Bundle result) throws Exception {
        RuntimeThreadFixture instance = new RuntimeThreadFixture();
        Object reference = new Object();
        instance.kept = 137;
        instance.reference = reference;
        byte[][] patches = {asset("runtime-thread-one", RuntimeThreadFixture.class),
                asset("runtime-thread-two", RuntimeThreadFixture.class)};
        long beforeRejectedRead = runtimeReferenceReads();
        rejectRuntimeThreadProbe();
        expect("unverified ordinary mapping rejected", 67,
                redefine(RuntimeThreadFixture.class, patches[0]));
        if (!DcevmRuntime.capabilities().getString("memberMapping").contains(
                "runtime Thread probe rejected injected mismatch"))
            throw new AssertionError("Missing shared Thread rejection diagnostic");
        ++checks;
        if (runtimeReferenceReads() != beforeRejectedRead)
            throw new AssertionError("Rejected Thread probe read heap references");
        ++checks;
        expect("failed ordinary transaction preserves method", 11, instance.value());
        expect("failed ordinary transaction preserves fields", 137, instance.kept);
        if (instance.reference != reference) throw new AssertionError("Rejected transaction changed reference");
        ++checks;
        AtomicBoolean stop = new AtomicBoolean();
        AtomicLong allocations = new AtomicLong();
        AtomicLong gcRequests = new AtomicLong();
        AtomicLong gcReturns = new AtomicLong();
        AtomicReference<Throwable> workerError = new AtomicReference<>();
        CountDownLatch started = new CountDownLatch(1);
        Thread allocator = new Thread(() -> {
            try {
                // Keep a reachable graph alive so CC has actual marking work;
                // byte-array churn alone often finishes marking between probes.
                Object[][] graph = new Object[65536][];
                for (int i = 0; i < graph.length; ++i)
                    graph[i] = new Object[] {new Object(), new Object(), reference};
                Object[] retained = new Object[2048];
                while (!stop.get()) {
                    for (int i = 0; i < retained.length; ++i) {
                        byte[] bytes = new byte[4096];
                        bytes[0] = (byte) i;
                        retained[i] = bytes;
                        allocations.incrementAndGet();
                    }
                    if (graph[(int) (allocations.get() % graph.length)][2] != reference)
                        throw new AssertionError("GC pressure graph lost its root");
                    // System.gc() coalesces requests for targetSdk <= 34.
                    // Runtime.gc() reaches the public native request directly.
                    gcRequests.incrementAndGet();
                    Runtime.getRuntime().gc();
                    gcReturns.incrementAndGet();
                    started.countDown();
                }
            } catch (Throwable error) {
                workerError.set(error);
            } finally {
                started.countDown();
            }
        }, "runtime-thread-gc");
        long gcBefore = Long.parseLong(Debug.getRuntimeStat("art.gc.gc-count"));
        long markingBefore = runtimeMarkingReads();
        int transactions = 0;
        long pressureReadyGcCycles;
        allocator.start();
        try {
            if (!started.await(30, TimeUnit.SECONDS)) throw new AssertionError("GC worker did not finish a request");
            if (workerError.get() != null) throw new AssertionError("GC worker failed", workerError.get());
            pressureReadyGcCycles = Long.parseLong(Debug.getRuntimeStat("art.gc.gc-count")) - gcBefore;
            // No transaction has started in this interval: ART's structural
            // transaction GC cannot satisfy worker readiness.
            if (gcReturns.get() == 0 || allocations.get() <= 256 || pressureReadyGcCycles <= 0)
                throw new AssertionError("Missing independent GC pressure before transactions");
            // Retry for at most 512 transactions / 120 seconds. CMC may never
            // report CC marking; a zero result is recorded, never called overlap.
            long overlapDeadline = SystemClock.uptimeMillis() + 120000;
            for (int cycle = 0; cycle < 512; ++cycle) {
                int version = 12 + cycle % 2;
                expect("ordinary transaction under GC", 0,
                        redefine(RuntimeThreadFixture.class, patches[cycle % 2]));
                expect("ordinary new body under GC", version, instance.value());
                expect("ordinary preserved primitive under GC", 137, instance.kept);
                if (instance.reference != reference) throw new AssertionError("GC changed reference identity");
                ++checks;
                expect("ordinary added field starts at zero", 0, RuntimeThreadFixture.class
                        .getField(cycle % 2 == 0 ? "one" : "two").getInt(instance));
                expect("ordinary added method under GC", version, (Integer) RuntimeThreadFixture.class
                        .getMethod(cycle % 2 == 0 ? "one" : "two").invoke(instance));
                if (!DcevmRuntime.capabilities().getString("memberMapping").startsWith("verified: "))
                    throw new AssertionError("Ordinary mapping did not verify under GC");
                ++checks;
                transactions = cycle + 1;
                if (transactions >= 32 && transactions % 2 == 0 &&
                        (runtimeMarkingReads() > markingBefore ||
                         SystemClock.uptimeMillis() >= overlapDeadline)) break;
            }
        } finally {
            stop.set(true);
            allocator.join(15000);
        }
        if (allocator.isAlive()) throw new AssertionError("GC worker did not stop");
        if (workerError.get() != null) throw new AssertionError("GC worker failed", workerError.get());
        long gcCycles = Long.parseLong(Debug.getRuntimeStat("art.gc.gc-count")) - gcBefore;
        if (runtimeReferenceReads() <= beforeRejectedRead)
            throw new AssertionError("Missing shared-reader observations");
        ++checks;
        Runtime.getRuntime().gc();
        expect("ordinary body after GC", 13, instance.value());
        expect("ordinary primitive after GC", 137, instance.kept);
        if (instance.reference != reference) throw new AssertionError("Reference lost after GC");
        ++checks;
        result.putInt("ordinaryTransactions", transactions);
        result.putBoolean("ordinaryMarkingOverlap", runtimeMarkingReads() > markingBefore);
        result.putLong("ordinaryGcCycles", gcCycles);
        // Requests/returns are not counts of distinct collections or overlap.
        // ordinaryGcCycles also includes ART's own structural-transaction GC.
        result.putLong("ordinaryPressureGcRequests", gcRequests.get());
        result.putLong("ordinaryPressureGcReturns", gcReturns.get());
        result.putLong("ordinaryPressureReadyGcCycles", pressureReadyGcCycles);
        result.putLong("ordinaryAllocations", allocations.get());
        result.putLong("ordinaryReferenceReads", runtimeReferenceReads() - beforeRejectedRead);
        // CC can report marking reads; userfaultfd CMC intentionally reports zero.
        result.putLong("ordinaryMarkingReads", runtimeMarkingReads() - markingBefore);
    }

    private Bundle arguments;
    private int checks;

    // Debuggable applications may start with CheckJNI even when debug.checkjni
    // is zero. Switch before the engine captures and guards the JNI table.
    static void configureRuntime(Bundle arguments) {
        if (arguments == null || !arguments.containsKey("checkjni")) return;
        System.loadLibrary("artdcevm");
        boolean enabled = Boolean.parseBoolean(arguments.getString("checkjni"));
        int code = configureCheckJni(enabled);
        if (code != 0) throw new AssertionError("Cannot select JNI checking mode: " + code);
    }

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); this.arguments = arguments; start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            configureRuntime(arguments);
            boolean ordinaryOnly = arguments != null && "ordinary".equals(arguments.getString("scenario"));
            boolean pointerRuntime = arguments != null && "interface-pointer-runtime".equals(arguments.getString("scenario"));
            boolean indexRuntime = arguments != null && "interface-index-runtime".equals(arguments.getString("scenario"));
            int primedPointerIds = 0;
            if (!ordinaryOnly && !pointerRuntime && !indexRuntime && (arguments == null || !arguments.containsKey("runtimeProbe"))) {
                int methodId = primePointerId(PointerIdFixture.Contract.class, 0);
                int instanceId = primePointerId(PointerIdFixture.Implementation.class, 1);
                int staticId = primePointerId(PointerIdFixture.Implementation.class, 2);
                if (methodId < 0 || instanceId < 0 || staticId < 0) throw new AssertionError("Cannot prepare JNI IDs");
                primedPointerIds = methodId + instanceId + staticId;
            }
            DcevmRuntime.initialize(getTargetContext());
            StandardJvmtiClient.attach(getTargetContext());
            expect("interface capability initially unverified", 0,
                    DcevmRuntime.capabilities().getBoolean("interfaceRedefinition") ? 1 : 0);
            if (pointerRuntime || indexRuntime) {
                checkJniIdRuntime(result, pointerRuntime);
                finish(Activity.RESULT_OK, result);
                return;
            }
            if (arguments != null && arguments.containsKey("runtimeProbe")) {
                int stage = Integer.parseInt(arguments.getString("runtimeProbe"));
                if (stage < 13 || stage > 15) throw new AssertionError("Unknown runtime probe");
                CopiedChild unchanged = new CopiedChild();
                expect("arm runtime probe", 0, armFailure(stage));
                expect("runtime probe rejection", 98, redefine(CopiedDefault.class,
                        asset("interface-default-body", CopiedDefault.class)));
                expect("runtime probe hit", 1, failureHits());
                String reason = DcevmRuntime.capabilities().getString("interfaceRuntime");
                if (!reason.startsWith("disabled: ") || !reason.contains(
                        stage == 13 ? "Thread" : stage == 14 ? "HandleScope" : "holder"))
                    throw new AssertionError("Missing runtime probe rejection: " + reason);
                ++checks;
                expect("interface capability disabled by probe", 0,
                        DcevmRuntime.capabilities().getBoolean("interfaceRedefinition") ? 1 : 0);
                String firstRejection = DcevmRuntime.capabilities().getString("lastRejection");
                if (!firstRejection.contains("CopiedDefault;") || !firstRejection.contains("probe rejected"))
                    throw new AssertionError("Missing first probe rejection class/reason");
                ++checks;
                expect("old dispatch after probe rejection", 91, unchanged.copiedValue());
                expect("disabled interface stays rejected", 98, redefine(CopiedDefault.class,
                        asset("interface-default-body", CopiedDefault.class)));
                String laterRejection = DcevmRuntime.capabilities().getString("lastRejection");
                if (!laterRejection.contains("CopiedDefault;") || !laterRejection.contains("probe rejected"))
                    throw new AssertionError("Missing disabled runtime rejection class/reason");
                ++checks;
                expect("old dispatch after disabled rejection", 91, unchanged.copiedValue());
                expect("new dispatch after disabled rejection", 91, new CopiedChild().copiedValue());
                expect("class transactions remain available", 0, redefine(CopiedParent.class,
                        asset("copied-default-add", CopiedParent.class)));
                System.gc(); System.runFinalization(); System.gc();
                expect("class dispatch after probe rejection", 92, unchanged.copiedValue());
                result.putInt("checks", checks);
                result.putBoolean("completed", true);
                finish(Activity.RESULT_OK, result);
                return;
            }
            checkRuntimeThread(result);
            if (ordinaryOnly) {
                result.putString("memberMapping", DcevmRuntime.capabilities().getString("memberMapping"));
                result.putInt("checks", checks);
                result.putBoolean("completed", true);
                finish(Activity.RESULT_OK, result);
                return;
            }
            checkDefaultConflictRollback();
            PointerIdFixture.Implementation pointerInstance = new PointerIdFixture.Implementation();
            byte[] pointerDex = asset("interface-pointer-body", PointerIdFixture.Contract.class);
            int naturalCode = redefine(PointerIdFixture.Contract.class, pointerDex);
            boolean naturalMarker = naturalCode == 79;
            result.putInt("primedPointerIds", primedPointerIds);
            result.putBoolean("naturalPointerMarker", naturalMarker);
            if (naturalMarker) {
                String reason = DcevmRuntime.capabilities().getString("lastRejection");
                if (!reason.contains("PointerIdFixture$") || !reason.contains("active pointer JNI IDs"))
                    throw new AssertionError("Missing natural pointer-id marker rejection class/reason");
                ++checks;
                expect("natural marker preserves dispatch", 51, pointerInstance.pointerValue());
                expect("natural marker preserves new dispatch", 51, new PointerIdFixture.Implementation().pointerValue());
            } else {
                expect("natural marker not produced", 0, naturalCode);
                expect("indexed IDs remain admissible", 52, pointerInstance.pointerValue());
            }
            InterfaceBatchFixture.Implementation batchInstance = new InterfaceBatchFixture.Implementation();
            InterfaceBatchFixture.Contract batchContract = batchInstance;
            InterfaceBatchFixture.Unrelated unrelated = new InterfaceBatchFixture.Unrelated();
            batchInstance.marker = 73; unrelated.marker = 79;
            byte[] batchBody = asset("interface-batch-body", InterfaceBatchFixture.Contract.class);
            byte[] batchNext = asset("interface-batch-next", InterfaceBatchFixture.Contract.class);
            byte[] unrelatedBody = asset("interface-batch-body", InterfaceBatchFixture.Unrelated.class);
            expect("batch baseline commit", 0, StandardJvmtiClient.redefine(
                    new Class<?>[]{InterfaceBatchFixture.Contract.class, InterfaceBatchFixture.Unrelated.class},
                    new byte[][]{batchBody, unrelatedBody}));
            expect("batch baseline interface", 12, batchContract.value());
            expect("batch baseline unrelated", 22, unrelated.value());
            expect("reset batch cleanup counters", 0, armFailure(0));
            expect("batch unrelated verifier failure", 62, StandardJvmtiClient.redefine(
                    new Class<?>[]{InterfaceBatchFixture.Unrelated.class, InterfaceBatchFixture.Contract.class},
                    new byte[][]{invalidReturn(unrelatedBody), batchNext}));
            String batchReason = DcevmRuntime.capabilities().getString("lastRejection");
            if (!batchReason.contains("InterfaceBatchFixture$Unrelated;") || !batchReason.contains("FAILS_VERIFICATION"))
                throw new AssertionError("Missing batch verifier rejection reason");
            ++checks;
            if (discardedClasses() < 2) throw new AssertionError("Failed batch did not discard interface replacements");
            ++checks;
            expect("failed batch interface", 12, batchContract.value());
            expect("failed batch virtual", 12, batchInstance.value());
            expect("failed batch unrelated", 22, unrelated.value());
            expect("failed batch new interface", 12, new InterfaceBatchFixture.Implementation().value());
            expect("failed batch new unrelated", 22, new InterfaceBatchFixture.Unrelated().value());
            expect("failed batch interface field", 73, batchInstance.marker);
            expect("failed batch unrelated field", 79, unrelated.marker);
            expect("interface after failed batch", 0, redefine(InterfaceBatchFixture.Contract.class, batchNext));
            expect("interface dispatch after failed batch", 13, batchContract.value());
            System.gc(); System.runFinalization(); System.gc();
            expect("batch interface after GC", 13, batchContract.value());
            InterfaceBatchFixture.Child batchChild = new InterfaceBatchFixture.Child();
            batchChild.marker = 79; batchChild.childMarker = 83;
            byte[] implementationBody = asset("interface-implementation-body", InterfaceBatchFixture.Implementation.class);
            byte[] interfaceBody = asset("interface-implementation-body", InterfaceBatchFixture.Contract.class);
            byte[] implementationAdd = asset("interface-implementation-add", InterfaceBatchFixture.Implementation.class);
            byte[] interfaceAdd = asset("interface-implementation-add", InterfaceBatchFixture.Contract.class);
            expect("implementation first batch", 0, StandardJvmtiClient.redefine(
                    new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                    new byte[][]{implementationBody, interfaceBody}));
            expect("observed implementation before interface", 1, allocationOrder());
            expect("implementation body updated", 114, batchInstance.ownValue());
            expect("interface body updated", 14, batchContract.value());
            expect("unsubmitted child relinked", 14, batchChild.value());
            for (int stage : new int[]{1, 2, 3, 4, 17}) {
                expect("arm implementation batch failure", 0, armFailure(stage));
                expect("implementation batch injected rejection", stage == 1 || stage == 4 ? 113 : 110,
                        StandardJvmtiClient.redefine(
                                new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                                new byte[][]{implementationAdd, interfaceAdd}));
                expect("implementation batch injection hit", 1, failureHits());
                if (discardedClasses() < 2) throw new AssertionError("Missing implementation batch cleanup");
                ++checks;
                String reason = DcevmRuntime.capabilities().getString("lastRejection");
                String detail = stage == 2 ? "injected failure after interface allocation"
                        : stage == 3 ? "injected instance allocation failure"
                        : stage == 17 ? "OUT_OF_MEMORY" : "INTERNAL";
                if (!reason.contains("InterfaceBatchFixture$Contract;") || !reason.contains(detail))
                    throw new AssertionError("Missing implementation batch rejection class/reason");
                ++checks;
                expect("failed implementation batch interface", 14, batchContract.value());
                expect("failed implementation batch body", 114, batchInstance.ownValue());
                expect("failed implementation batch child", 14, batchChild.value());
                expect("failed implementation batch field", 73, batchInstance.marker);
                expect("failed implementation batch new", 14, new InterfaceBatchFixture.Implementation().value());
                expect("implementation batch recovery", 0, StandardJvmtiClient.redefine(
                        new Class<?>[]{InterfaceBatchFixture.Contract.class, InterfaceBatchFixture.Implementation.class},
                        new byte[][]{interfaceBody, implementationBody}));
            }
            expect("arm implementation verifier test", 0, armFailure(0));
            expect("implementation actual DEX verification failure", 62, StandardJvmtiClient.redefine(
                    new Class<?>[]{InterfaceBatchFixture.Implementation.class, InterfaceBatchFixture.Contract.class},
                    new byte[][]{invalidReturn(implementationBody), interfaceAdd}));
            String implementationReason = DcevmRuntime.capabilities().getString("lastRejection");
            if (!implementationReason.contains("InterfaceBatchFixture$Implementation;") ||
                    !implementationReason.contains("FAILS_VERIFICATION"))
                throw new AssertionError("Missing implementation DEX rejection reason");
            ++checks;
            if (discardedClasses() < 3) throw new AssertionError("Verifier failure did not discard full implementation group");
            ++checks;
            expect("implementation verifier preserved interface", 14, batchContract.value());
            expect("implementation verifier preserved body", 114, batchInstance.ownValue());
            expect("implementation verifier preserved field", 73, batchInstance.marker);
            expect("implementation verifier preserved child", 14, batchChild.value());
            System.gc(); System.runFinalization(); System.gc();
            expect("interface after implementation verifier failure", 0,
                    redefine(InterfaceBatchFixture.Contract.class, interfaceBody));
            expect("implementation after verifier recovery", 114, batchInstance.ownValue());
            MultiInterfaceFixture.LeftImpl multiLeft = new MultiInterfaceFixture.LeftImpl();
            MultiInterfaceFixture.RightImpl multiRight = new MultiInterfaceFixture.RightImpl();
            byte[] multiLeftDex = asset("multi-interface-body", MultiInterfaceFixture.Left.class);
            byte[] multiRightDex = asset("multi-interface-body", MultiInterfaceFixture.Right.class);
            expect("arm second group failure", 0, armFailure(18));
            expect("second group allocation rejection", 110, StandardJvmtiClient.redefine(
                    new Class<?>[]{MultiInterfaceFixture.Left.class, MultiInterfaceFixture.Right.class},
                    new byte[][]{multiLeftDex, multiRightDex}));
            expect("second group failure hit", 1, failureHits());
            expect("both groups discarded", 4, discardedClasses());
            String multiReason = DcevmRuntime.capabilities().getString("lastRejection");
            if (!multiReason.contains("MultiInterfaceFixture$Right;") || !multiReason.contains("multiple interface groups"))
                throw new AssertionError("Missing multi-group class/reason");
            ++checks;
            expect("failed group left unchanged", 11, multiLeft.leftValue());
            expect("failed group right unchanged", 21, multiRight.rightValue());
            expect("failed group new left unchanged", 11, new MultiInterfaceFixture.LeftImpl().leftValue());
            expect("failed group new right unchanged", 21, new MultiInterfaceFixture.RightImpl().rightValue());
            expect("multi-group recovery", 0, StandardJvmtiClient.redefine(
                    new Class<?>[]{MultiInterfaceFixture.Left.class, MultiInterfaceFixture.Right.class},
                    new byte[][]{multiLeftDex, multiRightDex}));
            expect("multi-group old left", 12, multiLeft.leftValue());
            expect("multi-group old right", 22, multiRight.rightValue());
            expect("multi-group count", 4, allocatedTypes());
            MultiInterfaceFixture.FamilyImpl multiFamily = new MultiInterfaceFixture.FamilyImpl();
            MultiInterfaceFixture.FamilyChild multiChild = new MultiInterfaceFixture.FamilyChild();
            byte[] multiParentDex = asset("multi-interface-body", MultiInterfaceFixture.Parent.class);
            byte[] multiChildDex = asset("multi-interface-body", MultiInterfaceFixture.Child.class);
            expect("arm family verifier failure", 0, armFailure(0));
            expect("family child real verifier failure", 62, StandardJvmtiClient.redefine(
                    new Class<?>[]{MultiInterfaceFixture.Child.class, MultiInterfaceFixture.Parent.class},
                    new byte[][]{invalidReturn(multiChildDex), multiParentDex}));
            expect("family discarded once", 4, discardedClasses());
            expect("family old parent unchanged", 31, multiFamily.parentValue());
            expect("family old child unchanged", 41, multiFamily.childValue());
            expect("family subclass unchanged", 41, multiChild.childValue());
            String familyReason = DcevmRuntime.capabilities().getString("lastRejection");
            if (!familyReason.contains("MultiInterfaceFixture$Child;") || !familyReason.contains("FAILS_VERIFICATION"))
                throw new AssertionError("Missing family verifier rejection class/reason");
            ++checks;
            expect("family recovery", 0, StandardJvmtiClient.redefine(
                    new Class<?>[]{MultiInterfaceFixture.Child.class, MultiInterfaceFixture.Parent.class},
                    new byte[][]{multiChildDex, multiParentDex}));
            expect("family count once", 4, allocatedTypes());
            expect("family parent new body", 32, multiFamily.parentValue());
            expect("family child new body", 42, multiFamily.childValue());
            expect("family subclass new dispatch", 42, multiChild.childValue());
            System.gc(); System.runFinalization(); System.gc();
            expect("family after GC", 42, multiChild.childValue());
            MultiInterfaceFixture.FamilyImpl markerFamily = new MultiInterfaceFixture.FamilyImpl();
            for (int stage : new int[]{19, 20}) {
                expect("arm pointer marker result", 0, armFailure(stage));
                expect("pointer marker result rejected", 79, redefine(MultiInterfaceFixture.Parent.class, multiParentDex));
                expect("pointer marker injection hit", 1, failureHits());
                String reason = DcevmRuntime.capabilities().getString("lastRejection");
                if (!reason.contains("MultiInterfaceFixture$") || !reason.contains("active pointer JNI IDs"))
                    throw new AssertionError("Missing injected pointer-id class/reason");
                ++checks;
                expect("marker result old parent", 32, markerFamily.parentValue());
                expect("marker result old child", 42, markerFamily.childValue());
                expect("marker result new dispatch", 32, new MultiInterfaceFixture.FamilyImpl().parentValue());
                expect("marker result recovery", 0, redefine(MultiInterfaceFixture.Parent.class, multiParentDex));
            }
            CopiedChild child = new CopiedChild();
            CopiedDefault throughInterface = child;
            byte[] patch = asset("interface-default-body", CopiedDefault.class);
            byte[] parent = asset("copied-default-add", CopiedParent.class);
            byte[] invalidInvoke = asset("interface-invalid-invoke", CopiedDefault.class);
            NestedInterfaceHost.Implementation nested = new NestedInterfaceHost.Implementation();
            NestedInterfaceHost.Contract nestedContract = nested;
            nested.marker = 73;
            expect("nested baseline", 41, nestedContract.value());
            expect("nested InnerClass static flag", 1, java.lang.reflect.Modifier.isStatic(
                    NestedInterfaceHost.Contract.class.getModifiers()) ? 1 : 0);
            expect("nested body update", 0, redefine(NestedInterfaceHost.Contract.class,
                    asset("nested-interface-body", NestedInterfaceHost.Contract.class)));
            expect("nested old interface dispatch", 42, nestedContract.value());
            expect("nested old virtual dispatch", 42, nested.value());
            expect("nested new dispatch", 42, new NestedInterfaceHost.Implementation().value());
            expect("nested instance field", 73, nested.marker);
            expect("unsupported nested transaction", 63, redefine(NestedInterfaceHost.Contract.class,
                    asset("nested-interface-add", NestedInterfaceHost.Contract.class)));
            String rejection = DcevmRuntime.capabilities().getString("lastRejection");
            if (!rejection.startsWith("Lcom/github/ytlog/artdcevm/fixture/NestedInterfaceHost$Contract;: ")
                    || !rejection.contains("has no concrete public implementation")) throw new AssertionError("Missing nested rejection reason");
            ++checks;
            expect("nested unchanged after rejection", 42, nestedContract.value());
            expect("top-level after nested transactions", 0, redefine(CopiedDefault.class, patch));
            expect("top-level dispatch after nested transactions", 93, throughInterface.copiedValue());
            expect("interface capability verified after target rejection", 1,
                    DcevmRuntime.capabilities().getBoolean("interfaceRedefinition") ? 1 : 0);
            expect("runtime available after target rejection", 1,
                    DcevmRuntime.capabilities().getString("interfaceRuntime").startsWith("verified: ") ? 1 : 0);
            System.gc(); System.runFinalization(); System.gc();
            expect("nested dispatch after GC", 42, nestedContract.value());
            // Repeat in the same process: successful redefinitions make each
            // following failed attempt own a different DexFile and obsolete chain.
            for (int cycle = 0; cycle < 3; ++cycle) {
                for (int stage = 0; stage <= 5; ++stage) {
                    int before = throughInterface.copiedValue();
                    int parentBefore = child.parentValue();
                    boolean verifierFailure = stage == 0 || stage == 5;
                    expect("arm", 0, armFailure(verifierFailure ? 0 : stage));
                    int code = redefine(CopiedDefault.class,
                            stage == 0 ? invalidReturn(patch) : stage == 5 ? invalidInvoke : patch);
                    expect("failure stage " + stage, verifierFailure ? 62 : stage == 1 ? 113 : stage == 4 ? 113 : 110, code);
                    expect("injection hit", verifierFailure ? 0 : 1, failureHits());
                    // Interface and implementation must both be discarded,
                    // including when AllocateNewClassObject returned nullptr.
                    if (discardedClasses() < 2) throw new AssertionError("cleanup did not cover both private classes");
                    ++checks;
                    expect("old interface", before, throughInterface.copiedValue());
                    expect("old virtual", before, child.copiedValue());
                    expect("new child", before, new CopiedChild().copiedValue());
                    expect("old parent", parentBefore, child.parentValue());
                    if (cycle == 1 || stage == 5) { System.gc(); System.runFinalization(); System.gc(); }
                    // Exercise ART's heap scan of the original implementation
                    // parent BEFORE a GC can hide the dangling resolved class.
                    expect("parent redefine after failure", 0, redefine(CopiedParent.class, parent));
                    expect("success after failure", 0, redefine(CopiedDefault.class, patch));
                    expect("old interface after commit", 92, throughInterface.copiedValue());
                    expect("old virtual after commit", 92, child.copiedValue());
                    expect("new child after commit", 92, new CopiedChild().copiedValue());
                    System.gc(); System.runFinalization(); System.gc();
                    expect("old interface after GC", 92, throughInterface.copiedValue());
                    expect("redefine after GC", 0, redefine(CopiedDefault.class, patch));
                    expect("new child after GC", 92, new CopiedChild().copiedValue());
                }
            }
            // Exceptions injected at the exact callback/hook boundary must
            // return through ART normally, including before cleanup roots exist.
            for (int stage : new int[]{8, 11, 12, 16}) {
                expect("arm hook exception", 0, armFailure(stage));
                expect("hook exception " + stage, stage == 12 ? 113 : 110,
                        redefine(CopiedDefault.class, patch));
                expect("hook exception hit", 1, failureHits());
                expect("old dispatch after hook exception", 92, throughInterface.copiedValue());
                expect("parent after hook exception", 0, redefine(CopiedParent.class, parent));
                expect("interface after hook exception", 0, redefine(CopiedDefault.class, patch));
                System.gc(); System.runFinalization(); System.gc();
                expect("new dispatch after hook exception and GC", 92, new CopiedChild().copiedValue());
            }
            // The submitted bytes do not reference LateCopiedChild. A real
            // ClassFileLoadHook introduces it only after the loaded-class
            // snapshot; verification prepares it and the engine must retry.
            String late = "Lcom/github/ytlog/artdcevm/fixture/LateCopiedChild;";
            expect("late child initially unloaded", 0, StandardJvmtiClient.isLoaded(late) ? 1 : 0);
            long retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            long transformations = StandardJvmtiClient.classFileReplacements();
            expect("reset injection", 0, armFailure(0));
            expect("install transforming hook", 0, StandardJvmtiClient.setClassFileReplacement(
                    "com/github/ytlog/artdcevm/fixture/CopiedDefault", asset("interface-retry", CopiedDefault.class)));
            try {
                expect("interface retry", 0, redefine(CopiedDefault.class, patch));
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            expect("actual retry", 1, (int) (DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries));
            expect("hook rerun", 2, (int) (StandardJvmtiClient.classFileReplacements() - transformations));
            if (discardedClasses() < 2) throw new AssertionError("retry did not discard its private classes");
            ++checks;
            expect("late child now loaded", 1, StandardJvmtiClient.isLoaded(late) ? 1 : 0);
            CopiedDefault lateChild = (CopiedDefault) Class.forName("com.github.ytlog.artdcevm.fixture.LateCopiedChild")
                    .getConstructor().newInstance();
            expect("late child dispatch", 92, lateChild.copiedValue());
            expect("parent scan after retry", 0, redefine(CopiedParent.class, parent));
            System.gc(); System.runFinalization(); System.gc();
            expect("late child after GC", 92, lateChild.copiedValue());
            expect("interface after retry and GC", 0, redefine(CopiedDefault.class, patch));
            expect("old child after retry", 92, child.copiedValue());
            // A second implementation implements the target directly, without
            // CopiedParent. It is introduced after the Native subtype snapshot.
            String direct = "com.github.ytlog.artdcevm.fixture.LateSnapshotImplementation";
            String directDescriptor = "L" + direct.replace('.', '/') + ";";
            expect("direct implementation initially unloaded", 0, StandardJvmtiClient.isLoaded(directDescriptor) ? 1 : 0);
            retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            transformations = StandardJvmtiClient.classFileReplacements();
            expect("install direct implementation hook", 0, StandardJvmtiClient.setClassFileReplacement(
                    "com/github/ytlog/artdcevm/fixture/CopiedDefault", asset("interface-snapshot-retry", CopiedDefault.class)));
            try {
                expect("direct implementation retry", 0, redefine(CopiedDefault.class, patch));
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            expect("direct implementation actual retry", 1,
                    (int) (DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries));
            expect("direct implementation hook rerun", 2,
                    (int) (StandardJvmtiClient.classFileReplacements() - transformations));
            expect("direct implementation now loaded", 1, StandardJvmtiClient.isLoaded(directDescriptor) ? 1 : 0);
            CopiedDefault directChild = (CopiedDefault) Class.forName(direct).getConstructor().newInstance();
            expect("direct implementation after retry", 94, directChild.copiedValue());
            expect("restore after direct implementation retry", 0, redefine(CopiedDefault.class, patch));
            System.gc(); System.runFinalization(); System.gc();
            expect("direct implementation after GC", 93, directChild.copiedValue());
            // A Proxy implementation is kept and relinked to the new interface.
            Object proxy = java.lang.reflect.Proxy.newProxyInstance(CopiedDefault.class.getClassLoader(),
                    new Class<?>[]{CopiedDefault.class}, (object, method, values) -> 92);
            expect("Proxy implementation relinked", 0, redefine(CopiedDefault.class, patch));
            expect("Proxy remains usable", 92, ((CopiedDefault) proxy).copiedValue());
            expect("old child after Proxy relink", 92, child.copiedValue());
            result.putInt("checks", checks);
            result.putBoolean("completed", true);
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("error", error.toString());
            result.putInt("checks", checks);
            finish(Activity.RESULT_CANCELED, result);
        }
    }
    private void expect(String name, int expected, int actual) {
        if (actual != expected) throw new AssertionError(name + ": expected " + expected + ", got " + actual);
        ++checks;
    }
    private int redefine(Class<?> klass, byte[] dex) { return StandardJvmtiClient.redefine(new Class<?>[]{klass}, new byte[][]{dex}); }
    private byte[] asset(String variant, Class<?> klass) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(variant + "/" + klass.getName().replace('.', '/') + ".dex");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
    private static int u32(byte[] bytes, int at) {
        return (bytes[at] & 255) | (bytes[at + 1] & 255) << 8 | (bytes[at + 2] & 255) << 16 | (bytes[at + 3] & 255) << 24;
    }
    private static int uleb(byte[] bytes, int[] cursor) {
        int value = 0, shift = 0, next;
        do { next = bytes[cursor[0]++] & 255; value |= (next & 127) << shift; shift += 7; } while ((next & 128) != 0);
        return value;
    }
    private static byte[] invalidReturn(byte[] original) throws Exception {
        byte[] bytes = original.clone();
        int[] cursor = {u32(bytes, u32(bytes, 100) + 24)};
        int[] counts = new int[4];
        for (int i = 0; i < 4; ++i) counts[i] = uleb(bytes, cursor);
        for (int i = 0; i < counts[0] + counts[1]; ++i) { uleb(bytes, cursor); uleb(bytes, cursor); }
        if (counts[2] + counts[3] < 1) throw new AssertionError("Expected a method to invalidate");
        // The final encoded method is the fixture's int-returning virtual/default method.
        int code = 0;
        for (int i = 0; i < counts[2] + counts[3]; ++i) {
            uleb(bytes, cursor); uleb(bytes, cursor); code = uleb(bytes, cursor);
        }
        int instruction = code + 16 + u32(bytes, code + 12) * 2 - 2;
        if (code == 0 || bytes[instruction] != 0x0f) throw new AssertionError("Expected return-int");
        bytes[instruction] = 0x11; // return-object in an int method: real ART verifier failure.
        System.arraycopy(MessageDigest.getInstance("SHA-1").digest(java.util.Arrays.copyOfRange(bytes, 32, bytes.length)), 0, bytes, 12, 20);
        Adler32 checksum = new Adler32(); checksum.update(bytes, 12, bytes.length - 12);
        int sum = (int) checksum.getValue();
        for (int i = 0; i < 4; ++i) bytes[8 + i] = (byte) (sum >>> (i * 8));
        return bytes;
    }
}
