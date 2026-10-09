package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.FileInputStream;
import java.lang.reflect.Method;
import java.security.MessageDigest;

/** Self instrumentation for the real ARM32 receiver; no JVMTI/evolution attach. */
public final class Arm32ReceiverInstrumentation extends Instrumentation {
    private static final String SCOPE = "arm32-original-jni-readonly-metadata";
    private Bundle arguments;

    public static final class Carrier {
        public int instanceField;
        private long privateField;
        public static final int staticField=37;
        public Carrier() {}
        public Carrier(int ignored) {}
        public int value(int input) { return input + 41; }
        public static int staticValue(int input) { return input + 17; }
        private int privateValue(int input) { return input + 13; }
        public native int nativeValue(int input);
    }

    @Override public void onCreate(Bundle args) {
        arguments = args == null ? new Bundle() : new Bundle(args);
        super.onCreate(args);
        start();
    }

    private String installedHash() throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream input = new FileInputStream(getTargetContext().getApplicationInfo().sourceDir)) {
            byte[] bytes = new byte[32768];
            for (int count; (count = input.read(bytes)) != -1;) digest.update(bytes, 0, count);
        }
        StringBuilder result = new StringBuilder();
        for (byte value : digest.digest()) result.append(String.format(java.util.Locale.ROOT, "%02x", value & 255));
        return result.toString();
    }

    private JSONObject check(String role, int iteration, Method[] carriers) {
        JSONObject record = new JSONObject();
        try {
            record.put("role", role).put("iteration", iteration).put("pid", Process.myPid()).put("tid", Process.myTid());
            record.put("carrier", iteration == 0 ? "boot:Object.toString" : "app:Carrier.value");
            if (!Integer.valueOf(42).equals(carriers[1].invoke(new Carrier(), 1))) {
                throw new AssertionError("Owned bytecode carrier baseline mismatch");
            }
            JSONObject proof = new JSONObject(Arm32ReceiverProbe.run(carriers[iteration]));
            record.put("proof", proof);
            if (!Integer.valueOf(42).equals(carriers[1].invoke(new Carrier(), 1))) {
                throw new AssertionError("Owned bytecode carrier changed after JNI proof");
            }
            record.put("carrierPreserved", true);
        } catch (Throwable error) {
            try { record.put("carrierPreserved", false).put("error", error.toString()); }
            catch (Exception ignored) { }
        }
        return record;
    }

    @Override public void onStart() {
        JSONObject report = new JSONObject();
        JSONArray rows = new JSONArray();
        boolean passed = false;
        try {
            String runId = arguments.getString("proofRunId", "");
            String expectedHash = arguments.getString("proofApkSha256", "");
            report.put("schemaVersion", 1).put("scope", SCOPE).put("runId", runId)
                    .put("pid", Process.myPid()).put("sdkInt", Build.VERSION.SDK_INT)
                    .put("is64Bit", Process.is64Bit()).put("privateEvolutionInitialized", false)
                    .put("publicJvmtiInitialized", false);
            if (!runId.matches("[0-9a-f]{32}") || !expectedHash.matches("[0-9a-f]{64}")) {
                throw new IllegalArgumentException("Strict run UUID and APK SHA-256 are required");
            }
            String actualHash = installedHash();
            report.put("apkSha256", actualHash);
            if (!expectedHash.equals(actualHash)) throw new SecurityException("Installed APK SHA-256 mismatch");
            if (Process.is64Bit()) throw new UnsupportedOperationException("Actual ARM32 process required");
            Method[] carriers = {Object.class.getDeclaredMethod("toString"), Carrier.class.getDeclaredMethod("value", int.class)};
            JSONObject[][] results = new JSONObject[3][2];
            for (int i = 0; i < 2; ++i) results[0][i] = check("main", i, carriers);
            Thread[] workers = new Thread[2];
            for (int worker = 0; worker < 2; ++worker) {
                final int index = worker + 1;
                workers[worker] = new Thread(() -> {
                    for (int i = 0; i < 2; ++i) results[index][i] = check("worker" + index, i, carriers);
                }, "arm32-receiver-" + index);
                workers[worker].start();
            }
            for (Thread worker : workers) worker.join();
            int hits = 0, prefixes = 0;
            passed = true;
            for (JSONObject[] threadRows : results) for (JSONObject row : threadRows) {
                rows.put(row);
                JSONObject proof = row.optJSONObject("proof");
                passed &= row.optBoolean("carrierPreserved", false) && proof != null
                        && proof.optBoolean("passed", false) && proof.optBoolean("hookInstalled", false)
                        && proof.optBoolean("receiverReady", false) && proof.optBoolean("nativeOriginalJNIReceiverReady", false)
                        && proof.optBoolean("jniPrefixReadable", false) && proof.optInt("pointerBytes", 0) == 4
                        && proof.optInt("runnableScopeBytes", 0) == 12 && proof.optLong("jniMappedBytes", 0) >= 12
                        && proof.optInt("originalCallbackHits", 0) == 1 && proof.optInt("matchedOwnerPrefixes", 0) == 2;
                if (proof != null) {
                    hits += proof.optInt("originalCallbackHits", 0);
                    prefixes += proof.optInt("matchedOwnerPrefixes", 0);
                }
            }
            report.put("totalOriginalCallbackHits", hits).put("totalMatchedOwnerPrefixes", prefixes);
            java.util.ArrayList<java.lang.reflect.Executable> declarations = new java.util.ArrayList<>();
            java.util.Collections.addAll(declarations, Carrier.class.getDeclaredConstructors());
            java.util.Collections.addAll(declarations, Carrier.class.getDeclaredMethods());
            java.lang.reflect.Executable[] complete = declarations.toArray(new java.lang.reflect.Executable[0]);
            MethodDexOracle.Result oracle = MethodDexOracle.read(getTargetContext().getApplicationInfo().sourceDir, complete);
            if (oracle.directMethods + oracle.virtualMethods != complete.length) throw new AssertionError("Incomplete reflection method oracle");
            JSONObject metadata = new JSONObject(Arm32ReceiverProbe.metadata(Carrier.class, complete, oracle));
            metadata.put("expectedDeclaredMethods", complete.length).put("expectedDirectMethods", oracle.directMethods)
                    .put("expectedFields", oracle.fields.length).put("expectedStaticFields", oracle.staticFields)
                    .put("dexEntry", oracle.dexEntry).put("dexClassDefIndex", oracle.classDefIndex).put("dexTypeIndex", oracle.typeIndex);
            report.put("metadata", metadata);
            passed &= metadata.optBoolean("passed") && metadata.optBoolean("classProven")
                    && metadata.optInt("declaredMethods") == complete.length && metadata.optInt("directMethods") == oracle.directMethods;
        } catch (Throwable error) {
            passed = false;
            try { report.put("error", error.toString()); } catch (Exception ignored) { }
        }
        try { report.put("checks", rows).put("passed", passed); } catch (Exception ignored) { passed = false; }
        Bundle result = new Bundle();
        result.putString("stream", report.toString());
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, result);
    }
}
