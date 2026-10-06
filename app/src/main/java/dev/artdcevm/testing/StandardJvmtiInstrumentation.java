package dev.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;
import android.os.Debug;
import dev.artdcevm.api.DcevmRuntime;
import dev.artdcevm.fixture.Evolving;
import dev.artdcevm.fixture.Peer;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONObject;

/** Independent Agent conformance: this client only invokes the standard JVMTI function table. */
public final class StandardJvmtiInstrumentation extends Instrumentation {
    private static native int redefine(Class<?>[] classes, byte[][] dexes);
    private static native int redefineRestricted(Class<?>[] classes, byte[][] dexes);
    private boolean clientFirst;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        clientFirst = arguments != null && Boolean.parseBoolean(
                arguments.getString("clientFirst", "false"));
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
            System.loadLibrary("artdcevm_restricted_test");
            Debug.attachJvmtiAgent("libartdcevm_restricted_test.so", null,
                    getTargetContext().getClassLoader());
            require(redefineRestricted(new Class<?>[]{Evolving.class},
                    new byte[][]{patch("body")}) == 99, "capability denied");
            require(instance.compute() == 38, "capability denial atomicity");
            byte[] body = patch("body");
            AtomicInteger threadedResult = new AtomicInteger(-1);
            Thread worker = new Thread(() -> threadedResult.set(redefine(
                    new Class<?>[]{Evolving.class}, new byte[][]{body})), "standard-jvmti-client");
            worker.start();
            worker.join(10000);
            require(!worker.isAlive() && threadedResult.get() == 0, "threaded body result");
            require(instance.compute() == 39, "body behavior");
            require(redefine(new Class<?>[]{Evolving.class}, new byte[][]{patch("add")}) == 0,
                    "additive result");
            require(instance.compute() == 40 && Evolving.class.getField("added") != null,
                    "additive behavior");
            require(redefine(new Class<?>[]{Evolving.class, Peer.class},
                    new byte[][]{patch("delete"), new byte[]{1, 2, 3, 4}}) != 0,
                    "invalid batch result");
            require(instance.compute() == 40 && peer.compute() == 11, "batch atomicity");
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
        System.loadLibrary("artdcevm_client_test");
        Debug.attachJvmtiAgent("libartdcevm_client_test.so", null,
                getTargetContext().getClassLoader());
    }

    private byte[] patch(String name) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(
                name + "/dev/artdcevm/fixture/Evolving.dex");
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
