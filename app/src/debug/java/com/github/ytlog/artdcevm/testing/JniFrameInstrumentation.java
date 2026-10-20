package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.AtomicFile;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.JniFrameFixture;
import com.github.ytlog.artdcevm.fixture.JniFrameGate;
import com.github.ytlog.artdcevm.fixture.JniFrameProbe;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

/** Isolated JNI boundary checks with native callers or old Java callees held. */
public final class JniFrameInstrumentation extends Instrumentation {
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
            boolean holdNative = "native".equals(option("frame", "native"));
            int form = Integer.parseInt(option("form", "0"));
            if (form < 0 || form > 2) throw new IllegalArgumentException("Invalid JNI form");
            if (!holdNative && !"body".equals(option("frame", "native")))
                throw new IllegalArgumentException("Invalid frame kind");
            report.put("schemaVersion", 1).put("pid", Process.myPid())
                    .put("device", new JSONObject().put("model", Build.MODEL).put("api", Build.VERSION.SDK_INT)
                            .put("release", Build.VERSION.RELEASE))
                    .put("checkJni", Boolean.parseBoolean(option("checkjni", "false")))
                    .put("own", own).put("interfaceOwner", interfaceOwner).put("nonvirtual", nonvirtual)
                    .put("cacheBeforeAttach", beforeAttach).put("form", form)
                    .put("frame", holdNative ? "native" : "body").put("checks", checks);
            checkpoint("configure");
            InterfaceAbortInstrumentation.configureRuntime(arguments);
            Object receiver = own ? new JniFrameFixture.Own() : new JniFrameFixture.DefaultOnly();
            Class<?> type = receiver.getClass();
            Class<?> lookup = interfaceOwner ? JniFrameFixture.Contract.class : type;
            JniFrameFixture.Peer peer = new JniFrameFixture.Peer();
            int identity = System.identityHashCode(receiver);
            System.loadLibrary("artdcevm_client_test");
            if (beforeAttach) report.put("idTag", JniFrameProbe.cache(lookup));
            if ("true".equals(option("promote", "false"))) {
                StandardJvmtiClient.attach(getTargetContext());
                check("promoted", 0, JniReceiverLossInstrumentation.promoteIds());
            }
            DcevmRuntime.initialize(getTargetContext());
            StandardJvmtiClient.attach(getTargetContext());
            if (!beforeAttach) report.put("idTag", JniFrameProbe.cache(lookup));
            check("idAvailable", true, report.getInt("idTag") >= 0);
            check("threadPolicy", true, JniFrameProbe.prepareThreadPolicy());
            int baseline = nonvirtual && interfaceOwner ? 7 : own ? 11 : 7;
            invoke("baseline", receiver, nonvirtual, form, baseline);
            if ("pointer-reject".equals(option("admission", "apply"))) {
                report.put("admission", "pointer-reject");
                check("pointerId", 0, report.getInt("idTag"));
                for (int round = 0; round < 2; ++round) {
                    JniFrameGate.Parked[] frames = park(receiver, nonvirtual, form, holdNative, 3);
                    try {
                        check("r" + round + ".held", 3, frames.length);
                        for (int phase = 0; phase < 3; ++phase) {
                            String label = "r" + round + new String[]{"reject", "peerApply", "peerRestore"}[phase];
                            Class<?>[] targets = phase == 0 ? new Class<?>[]{type, JniFrameFixture.Peer.class}
                                    : new Class<?>[]{JniFrameFixture.Peer.class};
                            String variant = "jni-frames-" + (phase == 2 ? "original" : "drop");
                            check(label + ".code", phase == 0 ? 79 : 0, redefine(targets, variant));
                            state(label, receiver, type, peer, identity, own, true, phase == 1 ? 13 : 3);
                            resume(label, frames[phase], receiver, nonvirtual, form, baseline, baseline);
                        }
                    } finally { release(frames); }
                }
            } else {
                for (int round = 0; round < 2; ++round) {
                    // Start every invocation while the receiver is still compatible.
                    // Later old-body frames survive several commits and collections.
                    JniFrameGate.Parked[] frames = park(receiver, nonvirtual, form, holdNative, 4);
                    try {
                        check("r" + round + ".held", 4, frames.length);
                        for (int phase = 0; phase < 4; ++phase) {
                            boolean fail = phase % 2 == 0, changed = phase == 1 || phase == 2;
                            String label = "r" + round + new String[]{"abort", "apply", "restoreAbort", "restore"}[phase];
                            if (fail) check(label + ".armed", 0, InterfaceAbortInstrumentation.armMappingPlanFailure());
                            Class<?>[] targets = round == 0 ? new Class<?>[]{type, JniFrameFixture.Peer.class}
                                    : new Class<?>[]{JniFrameFixture.Peer.class, type};
                            check(label + ".code", fail ? 110 : 0,
                                    redefine(targets, "jni-frames-" + (phase < 2 ? "drop" : "original")));
                            if (fail) check(label + ".faultHits", 1, InterfaceAbortInstrumentation.mappingPlanFailureHits());
                            state(label, receiver, type, peer, identity, own, !changed, changed ? 13 : 3);
                            Object boundary = changed && !nonvirtual && !(own && !interfaceOwner)
                                    ? "java.lang.IncompatibleClassChangeError" : baseline;
                            resume(label, frames[phase], receiver, nonvirtual, form,
                                    holdNative ? boundary : baseline, boundary);
                        }
                    } finally { release(frames); }
                }
            }
            completed = true;
        } catch (Throwable error) {
            failures++;
            try { report.put("fatalError", error.getClass().getName())
                    .put("fatalTrace", android.util.Log.getStackTraceString(error)); } catch (Exception ignored) { }
        }
        try { report.put("completed", completed); checkpoint("finished"); } catch (Exception ignored) { }
        Bundle result = new Bundle();
        result.putBoolean("completed", completed); result.putInt("failures", failures);
        finish(completed && failures == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }
    private JniFrameGate.Parked[] park(Object receiver, boolean nonvirtual, int form,
                                      boolean holdNative, int count) throws Exception {
        JniFrameGate.Parked[] frames = new JniFrameGate.Parked[count];
        try {
            for (int i = 0; i < count; ++i) frames[i] = new JniFrameGate.Parked(
                    () -> JniFrameProbe.invoke(receiver, nonvirtual, form, holdNative), i);
            for (JniFrameGate.Parked frame : frames) frame.awaitHeld();
            return frames;
        } catch (Throwable error) { release(frames); throw error; }
    }
    private static void release(JniFrameGate.Parked[] frames) throws InterruptedException {
        for (JniFrameGate.Parked frame : frames) if (frame != null) frame.release();
        for (JniFrameGate.Parked frame : frames) if (frame != null) frame.close();
    }
    private int redefine(Class<?>[] targets, String variant) throws Exception {
        byte[][] definitions = new byte[targets.length][];
        for (int i = 0; i < targets.length; ++i) definitions[i] = asset(variant, targets[i]);
        return StandardJvmtiClient.redefine(targets, definitions);
    }
    private void state(String label, Object receiver, Class<?> type, JniFrameFixture.Peer peer,
                       int identity, boolean own, boolean compatible, int expectedPeer) throws Exception {
        check(label + ".interface", compatible, JniFrameFixture.Contract.class.isInstance(receiver));
        check(label + ".peer", expectedPeer, peer.value());
        check(label + ".identity", true, identity == System.identityHashCode(receiver));
        check(label + ".self", true, type.getField("self").get(receiver) == receiver);
        check(label + ".kept", own ? 23 : 19, type.getField("kept").getInt(receiver));
        System.gc(); System.runFinalization(); System.gc();
        check(label + ".gcIdentity", true, identity == System.identityHashCode(receiver));
        check(label + ".gcSelf", true, type.getField("self").get(receiver) == receiver);
        check(label + ".gcInterface", compatible, JniFrameFixture.Contract.class.isInstance(receiver));
    }
    private void resume(String label, JniFrameGate.Parked frame, Object receiver, boolean nonvirtual,
                        int form, Object expectedFrame, Object expectedBoundary) throws Exception {
        checkpoint(label + ".resumeBeforeCall");
        check(label + ".frame", expectedFrame, frame.finish());
        invoke(label, receiver, nonvirtual, form, expectedBoundary);
    }
    private void invoke(String label, Object receiver, boolean nonvirtual, int form, Object expected) throws Exception {
        checkpoint(label + ".followupBeforeCall");
        check(label + ".jni", expected, JniFrameGate.observe(() -> JniFrameProbe.invoke(receiver, nonvirtual, form, false)));
        checkpoint(label + ".afterCall");
    }
    private void check(String name, Object expected, Object actual) throws Exception {
        boolean passed = expected.equals(actual);
        if (!passed) failures++;
        checks.put(new JSONObject().put("name", name).put("expected", expected).put("actual", actual).put("passed", passed));
    }
    private void checkpoint(String stage) throws Exception {
        report.put("stage", stage).put("failures", failures);
        AtomicFile file = new AtomicFile(new File(getTargetContext().getFilesDir(), "jni-frames.json"));
        FileOutputStream stream = file.startWrite();
        try { stream.write(report.toString(2).getBytes(StandardCharsets.UTF_8)); file.finishWrite(stream); }
        catch (Throwable error) { file.failWrite(stream); throw error; }
    }
    private byte[] asset(String variant, Class<?> type) throws Exception {
        try (InputStream stream = getTargetContext().getAssets().open(variant + "/" + type.getName().replace('.', '/') + ".dex");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int count;
            while ((count = stream.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
}
