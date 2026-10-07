package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Debug;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.NarrowParent;
import com.github.ytlog.artdcevm.fixture.NarrowSubject;
import com.github.ytlog.artdcevm.fixture.Peer;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONObject;

/** Independent Agent conformance: this client only invokes the standard JVMTI function table. */
public final class StandardJvmtiInstrumentation extends Instrumentation {
    private static int redefine(Class<?>[] classes, byte[][] dexes) {
        return StandardJvmtiClient.redefine(classes, dexes);
    }
    private static native int redefineRestricted(Class<?>[] classes, byte[][] dexes);
    private static native int redefinePeer(Class<?>[] classes, byte[][] dexes);
    private static native int enableTransform(Class<?> target, byte[] dex);
    private static native int disableTransform();
    private boolean clientFirst;
    private boolean transformCase;
    private boolean transformHierarchyCase;
    private boolean publicOnly;
    private boolean directProbe;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        clientFirst = arguments != null && Boolean.parseBoolean(
                arguments.getString("clientFirst", "false"));
        transformCase = arguments != null && Boolean.parseBoolean(
                arguments.getString("transformCase", "false"));
        transformHierarchyCase = arguments != null && Boolean.parseBoolean(
                arguments.getString("transformHierarchyCase", "false"));
        publicOnly = arguments != null && Boolean.parseBoolean(
                arguments.getString("publicOnly", "false"));
        directProbe = arguments != null && Boolean.parseBoolean(
                arguments.getString("directProbe", "false"));
        start();
    }

    @Override public void onStart() {
        JSONObject report = new JSONObject();
        int status = 0;
        try {
            Evolving instance = new Evolving(37);
            Peer peer = new Peer();
            require(instance.compute() == 38, "baseline");
            if (clientFirst) attachClient();
            if (publicOnly) {
                DcevmRuntime.attachPublicJvmti(getTargetContext());
                DcevmRuntime.attachPublicJvmti(getTargetContext());
            } else {
                DcevmRuntime.initialize(getTargetContext());
                DcevmRuntime.initialize(getTargetContext());
            }
            if (!clientFirst) attachClient();
            JSONObject capabilities = new TestRuntimeBackend(getTargetContext()).capabilities();
            if (publicOnly && !capabilities.optBoolean("standardRedefineRouted")) {
                runPortableStandard(report, instance, peer);
                Bundle result = new Bundle();
                result.putString("stream", report.toString());
                finish(-1, result);
                return;
            }
            if (directProbe) {
                int addResult = redefine(new Class<?>[]{Evolving.class},
                        new byte[][]{patch("add")});
                require(addResult == 0, "direct probe setup");
                int directResult = StandardJvmtiClient.unsafeDirect(Evolving.class,
                        patch("delete"));
                boolean addedFieldPresent;
                try {
                    Evolving.class.getField("added");
                    addedFieldPresent = true;
                } catch (NoSuchFieldException expected) {
                    addedFieldPresent = false;
                }
                report.put("clientFirst", clientFirst)
                        .put("directResult", directResult)
                        .put("postDirectCompute", instance.compute())
                        .put("addedFieldPresent", addedFieldPresent)
                        .put("passed", true);
                Bundle result = new Bundle();
                result.putString("stream", report.toString());
                finish(-1, result);
                return;
            }
            if (transformHierarchyCase) {
                NarrowSubject subject = new NarrowSubject();
                NarrowParent oldAlias = subject;
                require(oldAlias.inherited() == 14, "hierarchy baseline");
                require(enableTransform(NarrowSubject.class, asset(
                        "hier-narrow-simple/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex")) == 0,
                        "enable hierarchy transform");
                int transformedResult;
                int callbackCount;
                try {
                    transformedResult = redefine(new Class<?>[]{NarrowSubject.class},
                            new byte[][]{asset("hier-narrow-restore/com/github/ytlog/artdcevm/fixture/NarrowSubject.dex")});
                } finally {
                    callbackCount = disableTransform();
                }
                boolean oldCallRejected = false;
                try { oldAlias.inherited(); }
                catch (IncompatibleClassChangeError expected) { oldCallRejected = true; }
                report.put("transformHierarchyResult", transformedResult)
                        .put("transformHierarchyCallbacks", callbackCount)
                        .put("oldCallRejected", oldCallRejected);
                require(transformedResult == 0 && callbackCount == 1, "hierarchy transform result");
                require(NarrowSubject.class.getSuperclass() == Object.class &&
                        subject.compute() == 19 && oldCallRejected, "hierarchy transform behavior");
                report.put("clientFirst", clientFirst).put("publicOnly", publicOnly)
                        .put("passed", true);
                Bundle result = new Bundle();
                result.putString("stream", report.toString());
                finish(-1, result);
                return;
            }
            if (transformCase) {
                require(enableTransform(Evolving.class, patch("add")) == 0, "enable transform");
                int transformedResult;
                int callbackCount;
                try {
                    transformedResult = redefine(new Class<?>[]{Evolving.class},
                            new byte[][]{patch("body")});
                } finally {
                    callbackCount = disableTransform();
                }
                report.put("transformResult", transformedResult);
                report.put("transformCallbacks", callbackCount);
                report.put("postTransformCompute", instance.compute());
                require(transformedResult == 0 && callbackCount == 1,
                        "transformed redefine result and callback count");
                require(instance.compute() == 40 && Evolving.class.getField("added") != null,
                        "transformed additive behavior");
                report.put("clientFirst", clientFirst).put("publicOnly", publicOnly)
                        .put("passed", true);
                Bundle result = new Bundle();
                result.putString("stream", report.toString());
                finish(-1, result);
                return;
            }
            System.loadLibrary("artdcevm_second_client_test");
            Debug.attachJvmtiAgent("libartdcevm_second_client_test.so", null,
                    getTargetContext().getClassLoader());
            System.loadLibrary("artdcevm_restricted_test");
            Debug.attachJvmtiAgent("libartdcevm_restricted_test.so", null,
                    getTargetContext().getClassLoader());
            require(redefineRestricted(new Class<?>[]{Evolving.class},
                    new byte[][]{patch("body")}) == 99, "capability denied");
            require(instance.compute() == 38, "capability denial atomicity");
            byte[] body = patch("body");
            AtomicInteger threadedResult = new AtomicInteger(-1);
            AtomicInteger peerResult = new AtomicInteger(-1);
            CountDownLatch start = new CountDownLatch(1);
            Thread worker = new Thread(() -> {
                try {
                    start.await();
                    threadedResult.set(redefine(new Class<?>[]{Evolving.class},
                            new byte[][]{body}));
                } catch (InterruptedException exception) { Thread.currentThread().interrupt(); }
            }, "standard-jvmti-client");
            Thread peerWorker = new Thread(() -> {
                try {
                    start.await();
                    peerResult.set(redefinePeer(new Class<?>[]{Peer.class},
                            new byte[][]{asset("peer-added/com/github/ytlog/artdcevm/fixture/Peer.dex")}));
                } catch (Exception exception) { throw new AssertionError(exception); }
            }, "second-jvmti-client");
            worker.start();
            peerWorker.start();
            start.countDown();
            worker.join(10000);
            peerWorker.join(10000);
            require(!worker.isAlive() && threadedResult.get() == 0, "threaded body result: alive=" + worker.isAlive() + ", error=" + threadedResult.get() + ", peerAlive=" + peerWorker.isAlive() + ", peerError=" + peerResult.get());
            require(!peerWorker.isAlive() && peerResult.get() == 0, "second client additive result");
            require(instance.compute() == 39, "body behavior");
            require(peer.compute() == 12 && Peer.class.getField("marker") != null,
                    "second client additive behavior");
            require(redefine(new Class<?>[]{Evolving.class}, new byte[][]{patch("add")}) == 0,
                    "additive result");
            require(instance.compute() == 40 && Evolving.class.getField("added") != null,
                    "additive behavior");
            require(redefine(new Class<?>[]{Evolving.class, Peer.class},
                    new byte[][]{patch("delete"), new byte[]{1, 2, 3, 4}}) != 0,
                    "invalid batch result");
            require(instance.compute() == 40 && peer.compute() == 12, "batch atomicity");
            if (publicOnly) {
                report.put("clientFirst", clientFirst).put("publicOnly", true).put("passed", true);
                Bundle result = new Bundle();
                result.putString("stream", report.toString());
                finish(-1, result);
                return;
            }
            require(redefine(new Class<?>[]{Evolving.class}, new byte[][]{patch("delete")}) == 0,
                    "evolution result");
            require(instance.compute() == 41, "evolution behavior");
            report.put("clientFirst", clientFirst);
            report.put("passed", true);
        } catch (Throwable failure) {
            status = 1;
            try {
                report.put("passed", false).put("error", android.util.Log.getStackTraceString(failure));
            } catch (org.json.JSONException ignored) { }
        }
        Bundle result = new Bundle();
        result.putString("stream", report.toString());
        finish(status == 0 ? -1 : 0, result);
    }

    private void attachClient() throws Exception {
        StandardJvmtiClient.attach(getTargetContext());
    }

    /** Standard ART rejects schema changes; only the calibrated enhanced route widens it. */
    private void runPortableStandard(JSONObject report, Evolving instance, Peer peer) throws Exception {
        report.put("clientFirst", clientFirst).put("publicOnly", true)
                .put("standardRedefineRouted", false);
        if (transformCase) {
            require(enableTransform(Evolving.class, patch("body")) == 0, "enable portable transform");
            int code;
            int callbacks;
            try {
                // The callback removes the requested added field before standard ART validation.
                code = redefine(new Class<?>[]{Evolving.class}, new byte[][]{patch("add")});
            } finally {
                callbacks = disableTransform();
            }
            report.put("transformResult", code).put("transformCallbacks", callbacks);
            require(code == 0 && callbacks == 1 && instance.compute() == 39,
                    "portable transform body and state");
            try {
                Evolving.class.getField("added");
                throw new AssertionError("portable callback left an added field");
            } catch (NoSuchFieldException expected) { }
        } else {
            System.loadLibrary("artdcevm_second_client_test");
            Debug.attachJvmtiAgent("libartdcevm_second_client_test.so", null,
                    getTargetContext().getClassLoader());
            System.loadLibrary("artdcevm_restricted_test");
            Debug.attachJvmtiAgent("libartdcevm_restricted_test.so", null,
                    getTargetContext().getClassLoader());
            require(redefineRestricted(new Class<?>[]{Evolving.class},
                    new byte[][]{patch("body")}) == 99, "portable capability denial");
            require(instance.compute() == 38, "portable denial atomicity");
            byte[] body = patch("body");
            byte[] peerBody = asset("peer-body/com/github/ytlog/artdcevm/fixture/Peer.dex");
            AtomicInteger code = new AtomicInteger(-1);
            AtomicInteger peerCode = new AtomicInteger(-1);
            CountDownLatch start = new CountDownLatch(1);
            Thread worker = new Thread(() -> {
                try { start.await(); code.set(redefine(new Class<?>[]{Evolving.class},
                        new byte[][]{body})); }
                catch (InterruptedException failure) { Thread.currentThread().interrupt(); }
            }, "portable-client");
            Thread peerWorker = new Thread(() -> {
                try { start.await(); peerCode.set(redefinePeer(new Class<?>[]{Peer.class},
                        new byte[][]{peerBody})); }
                catch (InterruptedException failure) { Thread.currentThread().interrupt(); }
            }, "portable-peer");
            worker.start(); peerWorker.start(); start.countDown();
            worker.join(10000); peerWorker.join(10000);
            report.put("bodyResult", code.get()).put("peerBodyResult", peerCode.get());
            require(!worker.isAlive() && !peerWorker.isAlive(), "portable concurrent workers completed");
            // ART rejects overlapping redefinitions with INTERNAL (113) while one is in progress.
            // Verify the rejected transaction did not commit, then retry after both workers exit.
            require((code.get() == 0 || code.get() == 113) &&
                    (peerCode.get() == 0 || peerCode.get() == 113) &&
                    (code.get() == 0 || peerCode.get() == 0), "portable concurrent Agent results");
            require(instance.compute() == (code.get() == 0 ? 39 : 38) &&
                    peer.compute() == (peerCode.get() == 0 ? 12 : 11), "portable contention atomicity");
            if (code.get() == 113)
                require(redefine(new Class<?>[]{Evolving.class}, new byte[][]{body}) == 0,
                        "portable body retry after contention");
            if (peerCode.get() == 113)
                require(redefinePeer(new Class<?>[]{Peer.class}, new byte[][]{peerBody}) == 0,
                        "portable peer retry after contention");
            report.put("contentionRetry", code.get() == 113 || peerCode.get() == 113);
            require(instance.compute() == 39 && peer.compute() == 12, "portable concurrent behavior");
            int schemaCode = redefine(new Class<?>[]{Evolving.class}, new byte[][]{patch("add")});
            report.put("standardSchemaResult", schemaCode);
            require(schemaCode == 64 && instance.compute() == 39, "portable schema refusal");
            int invalidCode = redefine(new Class<?>[]{Evolving.class, Peer.class},
                    new byte[][]{patch("portable-body"), new byte[]{1, 2, 3, 4}});
            report.put("invalidBatchResult", invalidCode);
            require(invalidCode != 0 && instance.compute() == 39 && peer.compute() == 12,
                    "portable batch rollback");
            require(redefine(new Class<?>[]{Evolving.class, Peer.class},
                    new byte[][]{patch("portable-body"), peerBody}) == 0,
                    "portable valid batch after rejection");
            require(instance.compute() == 44 && peer.compute() == 12, "portable valid batch behavior");
        }
        report.put("passed", true);
    }

    private byte[] patch(String name) throws Exception {
        return asset(name + "/com/github/ytlog/artdcevm/fixture/Evolving.dex");
    }

    private byte[] asset(String path) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(path);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int length;
            while ((length = input.read(buffer)) >= 0) output.write(buffer, 0, length);
            return output.toByteArray();
        }
    }

    private static void require(boolean value, String check) {
        if (!value) throw new AssertionError(check);
    }
}
