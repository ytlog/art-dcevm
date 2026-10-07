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

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        clientFirst = arguments != null && Boolean.parseBoolean(
                arguments.getString("clientFirst", "false"));
        transformCase = arguments != null && Boolean.parseBoolean(
                arguments.getString("transformCase", "false"));
        transformHierarchyCase = arguments != null && Boolean.parseBoolean(
                arguments.getString("transformHierarchyCase", "false"));
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
            DcevmRuntime.initialize(getTargetContext());
            DcevmRuntime.initialize(getTargetContext());
            if (!clientFirst) attachClient();
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
                report.put("clientFirst", clientFirst).put("passed", true);
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
                report.put("clientFirst", clientFirst).put("passed", true);
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
            require(!worker.isAlive() && threadedResult.get() == 0, "threaded body result");
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
