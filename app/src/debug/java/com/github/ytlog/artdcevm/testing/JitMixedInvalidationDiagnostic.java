package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import dalvik.system.DexClassLoader;
import java.io.File;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import org.json.JSONArray;
import org.json.JSONObject;

/** Mixed native structural/body updates with retained receivers and real JIT warmup. */
public final class JitMixedInvalidationDiagnostic extends Instrumentation {
    public static native long warmBuilder(int count);
    public static native long warmTarget(Object target, Method value, int count);

    private final JSONObject report = new JSONObject();
    private File directory;
    private boolean engine;

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        engine = arguments != null && "true".equals(arguments.getString("engine"));
        start();
    }

    private void save() throws Exception {
        Files.write(new File(directory, "report.json").toPath(),
                report.toString(2).getBytes(StandardCharsets.UTF_8));
    }

    private byte[] dex(String name) throws Exception {
        return Files.readAllBytes(new File(directory, name).toPath());
    }

    @Override public void onStart() {
        try {
            directory = new File(getTargetContext().getFilesDir(), "jit-mixed");
            report.put("pid", Process.myPid()).put("api", Build.VERSION.SDK_INT)
                    .put("engineRequested", engine).put("complete", false);
            save();
            if (engine) {
                com.github.ytlog.artdcevm.api.DcevmRuntime.initialize(getTargetContext());
            }
            StandardJvmtiClient.attach(getTargetContext());
            String maps = new String(Files.readAllBytes(new File("/proc/self/maps").toPath()),
                    StandardCharsets.UTF_8);
            boolean loaded = maps.contains("libartdcevm.so");
            report.put("engineLoaded", loaded);
            if (loaded != engine) throw new AssertionError("Engine mode mismatch");
            ClassLoader loader = new DexClassLoader(
                    new File(directory, "h0.dex").getPath() + ":"
                            + new File(directory, "v0.dex").getPath(),
                    getTargetContext().getCodeCacheDir().getPath(), null,
                    getTargetContext().getClassLoader());
            Object hot = loader.loadClass("baseline.Hot").getConstructor().newInstance();
            Object target = loader.loadClass("baseline.Target").getConstructor().newInstance();
            JSONArray rounds = new JSONArray();
            report.put("rounds", rounds);
            save();
            for (int i = 1; i <= 32; i++) {
                long builder = warmBuilder(100000);
                if (builder != 700000L) throw new AssertionError("Builder checksum " + builder);
                long sum = warmTarget(hot, hot.getClass().getMethod("value"), 100000);
                Thread.sleep(200);
                if (sum != 100000L * (i - 1)) throw new AssertionError("Hot checksum " + sum);
                Log.i("JitMixedDiagnostic", "structural round=" + i);
                report.put("pendingPhase", "structural").put("pendingRound", i);
                save();
                int structural = StandardJvmtiClient.redefineStructurally(
                        new Class<?>[]{target.getClass()}, new byte[][]{dex("v" + i + ".dex")});
                if (structural != 0) throw new AssertionError("Structural error " + structural);
                Log.i("JitMixedDiagnostic", "body round=" + i);
                report.put("pendingPhase", "body");
                save();
                int body = StandardJvmtiClient.redefineStructurally(
                        new Class<?>[]{hot.getClass()}, new byte[][]{dex("h" + i + ".dex")});
                if (body != 0) throw new AssertionError("Body error " + body);
                Object value = hot.getClass().getMethod("value").invoke(hot);
                if (!Integer.valueOf(i).equals(value)) {
                    throw new AssertionError("Retained hot receiver " + value);
                }
                rounds.put(new JSONObject().put("round", i).put("builderChecksum", builder)
                        .put("hotChecksum", sum).put("value", value));
                save();
            }
            report.put("complete", true).put("result", "PASS");
            save();
            finish(0, new Bundle());
        } catch (Throwable failure) {
            try {
                report.put("result", "FAIL").put("error", failure.toString());
                save();
            } catch (Exception ignored) {
                // Preserve the instrumentation failure even if report storage also failed.
            }
            Log.e("JitMixedDiagnostic", "Failed", failure);
            finish(1, new Bundle());
        }
    }
}
