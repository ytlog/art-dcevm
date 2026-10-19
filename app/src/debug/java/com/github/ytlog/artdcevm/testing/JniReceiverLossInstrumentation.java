package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.AtomicFile;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.ReceiverLossFixture;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

/** Each invalid CheckJNI receiver runs in its own process with durable checkpoints. */
public final class JniReceiverLossInstrumentation extends Instrumentation {
    private static native int promoteIds();
    private Bundle arguments;
    private final JSONObject report = new JSONObject();
    private final JSONArray checks = new JSONArray();
    private int failures;

    @Override public void onCreate(Bundle args) { super.onCreate(args); arguments = args; start(); }

    private String option(String key, String fallback) { return arguments.getString(key, fallback); }

    @Override public void onStart() {
        boolean completed = false;
        try {
            boolean own = "own".equals(option("receiver", "default"));
            boolean interfaceOwner = "interface".equals(option("owner", "interface"));
            boolean nonvirtual = "nonvirtual".equals(option("call", "virtual"));
            boolean beforeAttach = "before".equals(option("cache", "after"));
            int form = Integer.parseInt(option("form", "0"));
            if (form < 0 || form > 2) throw new IllegalArgumentException("Invalid JNI form");
            report.put("schemaVersion", 1).put("pid", Process.myPid())
                    .put("device", new JSONObject().put("model", Build.MODEL).put("api", Build.VERSION.SDK_INT)
                            .put("release", Build.VERSION.RELEASE))
                    .put("checkJni", Boolean.parseBoolean(option("checkjni", "false")))
                    .put("own", own).put("interfaceOwner", interfaceOwner).put("nonvirtual", nonvirtual)
                    .put("cacheBeforeAttach", beforeAttach).put("form", form).put("checks", checks);
            checkpoint("configure");
            InterfaceAbortInstrumentation.configureRuntime(arguments);
            Object receiver = own ? new ReceiverLossFixture.Own() : new ReceiverLossFixture.DefaultOnly();
            Class<?> type = receiver.getClass();
            Class<?> lookup = interfaceOwner ? ReceiverLossFixture.Contract.class : type;
            ReceiverLossFixture.Peer peer = new ReceiverLossFixture.Peer();
            int identity = System.identityHashCode(receiver);
            System.loadLibrary("artdcevm_client_test");
            if (beforeAttach) report.put("idTag", StandardJvmtiClient.cacheReceiverLossId(lookup));
            if ("true".equals(option("promote", "false"))) {
                // Load the JVMTI plugin through a real public agent before the
                // probe requests its temporary suspension environment.
                StandardJvmtiClient.attach(getTargetContext());
                checkpoint("promote.before");
                int promoted = promoteIds();
                check("promoted", 0, promoted);
                if (promoted != 0) throw new IllegalStateException("JNI ID probe promotion failed");
                checkpoint("promote.after");
            }
            DcevmRuntime.initialize(getTargetContext());
            StandardJvmtiClient.attach(getTargetContext());
            if (!beforeAttach) report.put("idTag", StandardJvmtiClient.cacheReceiverLossId(lookup));
            check("idAvailable", true, report.getInt("idTag") >= 0);
            int baseline = nonvirtual && interfaceOwner ? 7 : own ? 11 : 7;
            invoke("baseline", receiver, nonvirtual, form, baseline);
            if ("pointer-reject".equals(option("admission", "apply"))) {
                report.put("admission", "pointer-reject");
                check("pointerId", 0, report.getInt("idTag"));
                for (int round = 0; round < 2; ++round) {
                    String label = "reject" + round;
                    check(label + ".code", 79, StandardJvmtiClient.redefine(
                            new Class<?>[]{type, ReceiverLossFixture.Peer.class},
                            new byte[][]{asset("receiver-loss-drop", type), asset("receiver-loss-drop", ReceiverLossFixture.Peer.class)}));
                    check(label + ".interface", true, ReceiverLossFixture.Contract.class.isInstance(receiver));
                    check(label + ".peer", 3, peer.value());
                    check(label + ".identity", true, identity == System.identityHashCode(receiver));
                    check(label + ".self", true, type.getField("self").get(receiver) == receiver);
                    check(label + ".kept", own ? 23 : 19, type.getField("kept").getInt(receiver));
                    invoke(label, receiver, nonvirtual, form, baseline);
                    System.gc(); System.runFinalization(); System.gc();
                    invoke(label + ".gc", receiver, nonvirtual, form, baseline);
                    for (boolean restore : new boolean[]{false, true}) {
                        String variant = "receiver-loss-" + (restore ? "original" : "drop");
                        String p = label + (restore ? ".followRestore" : ".followApply");
                        check(p + ".code", 0, StandardJvmtiClient.redefine(new Class<?>[]{ReceiverLossFixture.Peer.class},
                                new byte[][]{asset(variant, ReceiverLossFixture.Peer.class)}));
                        check(p + ".peer", restore ? 3 : 13, peer.value());
                        invoke(p, receiver, nonvirtual, form, baseline);
                    }
                }
                completed = true;
                report.put("completed", true); checkpoint("finished");
                Bundle result = new Bundle(); result.putBoolean("completed", true); result.putInt("failures", failures);
                finish(failures == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
                return;
            }
            for (int round = 0; round < 2; ++round) for (int phase = 0; phase < 4; ++phase) {
                boolean fail = phase % 2 == 0, restoring = phase >= 2;
                boolean changed = phase == 1 || phase == 2;
                String label = "r" + round + new String[]{"abort", "apply", "restoreAbort", "restore"}[phase];
                String variant = "receiver-loss-" + (restoring ? "original" : "drop");
                if (fail) check(label + ".armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
                Class<?>[] targets = round == 0 ? new Class<?>[]{type, ReceiverLossFixture.Peer.class}
                        : new Class<?>[]{ReceiverLossFixture.Peer.class, type};
                byte[][] definitions = {asset(variant, targets[0]), asset(variant, targets[1])};
                int code = StandardJvmtiClient.redefine(targets, definitions);
                check(label + ".code", fail ? 110 : 0, code);
                if (fail) check(label + ".faultHits", 1, InterfaceAbortInstrumentation.mappingPlanFailureHits());
                check(label + ".interface", !changed, ReceiverLossFixture.Contract.class.isInstance(receiver));
                check(label + ".peer", changed ? 13 : 3, peer.value());
                check(label + ".identity", true, identity == System.identityHashCode(receiver));
                check(label + ".self", true, type.getField("self").get(receiver) == receiver);
                check(label + ".kept", own ? 23 : 19, type.getField("kept").getInt(receiver));
                Object expected = changed && !nonvirtual && !(own && !interfaceOwner)
                        ? "java.lang.IncompatibleClassChangeError" : baseline;
                // Persist the successful transition before an intentionally invalid JNI call.
                invoke(label, receiver, nonvirtual, form, expected);
                System.gc(); System.runFinalization(); System.gc();
                invoke(label + ".gc", receiver, nonvirtual, form, expected);
            }
            completed = true;
        } catch (Throwable error) {
            failures++;
            try { report.put("fatalError", error.getClass().getName()); } catch (Exception ignored) { }
        }
        try { report.put("completed", completed); checkpoint("finished"); } catch (Exception ignored) { }
        Bundle result = new Bundle();
        result.putBoolean("completed", completed); result.putInt("failures", failures);
        finish(completed && failures == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }

    private void invoke(String name, Object receiver, boolean nonvirtual, int form, Object expected) throws Exception {
        checkpoint(name + ".beforeCall");
        Object actual;
        try { actual = StandardJvmtiClient.callReceiverLossInt(receiver, nonvirtual, form); }
        catch (Throwable error) { actual = error.getClass().getName(); }
        check(name + ".jni", expected, actual);
        checkpoint(name + ".afterCall");
    }

    private void check(String name, Object expected, Object actual) throws Exception {
        boolean passed = expected.equals(actual);
        if (!passed) failures++;
        checks.put(new JSONObject().put("name", name).put("expected", expected).put("actual", actual).put("passed", passed));
    }

    private void checkpoint(String stage) throws Exception {
        report.put("stage", stage).put("failures", failures);
        AtomicFile file = new AtomicFile(new File(getTargetContext().getFilesDir(), "jni-receiver-loss.json"));
        FileOutputStream output = file.startWrite();
        try {
            output.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            file.finishWrite(output);
        } catch (Throwable error) { file.failWrite(output); throw error; }
    }

    private byte[] asset(String variant, Class<?> type) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(variant + "/" + type.getName().replace('.', '/') + ".dex");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
}
