package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.lang.reflect.Method;
import org.json.JSONObject;

/** Fresh-process regression for declaration-equivalent EVOLVE_SCHEMA's public route. */
public final class MvpBodyEvolutionSuite {
    private final Context context;
    private int checks;
    public MvpBodyEvolutionSuite(Context context) { this.context = context; }

    public JSONObject run() {
        checks = 0;
        JSONObject report = new JSONObject();
        boolean retained = false;
        try {
            JSONObject caps = DcevmRuntime.attachPublicJvmti(context);
            report.put("suite", "mvp-body-evolution").put("sdk", Build.VERSION.SDK_INT)
                    .put("abi", Build.SUPPORTED_ABIS[0])
                    .put("pageSize", Os.sysconf(OsConstants._SC_PAGESIZE))
                    .put("fingerprint", Build.FINGERPRINT).put("capabilities", caps);
            require(caps.getBoolean("methodBody"), "public method-body capability");
            require(!caps.optBoolean("evolutionAvailable") && !caps.optBoolean("privateHooksSupported"),
                    "fresh process has no initialized private schema owner");
            System.loadLibrary("artdcevm_test_support");
            Evolving instance = new Evolving(41);
            Method cached = Evolving.class.getMethod("compute");
            Evolving.counter = 113;
            NativeTestBridge.retainFixture(instance);
            retained = true;
            check(instance, cached, 42);
            int first = evolve("body");
            report.put("firstCode", first);
            require(first == 0, "same-declaration evolution reaches public JVMTI");
            check(instance, cached, 43);
            report.put("evolvedValue", instance.compute());
            int added = evolve("add");
            int hierarchy = evolve("hierarchy");
            report.put("schemaCode", added).put("hierarchyCode", hierarchy);
            require(added == 98 && hierarchy == 98, "unprepared schema and hierarchy remain unavailable");
            check(instance, cached, 43);
            require(Evolving.class.getSuperclass() == Object.class && Evolving.class.getInterfaces().length == 0,
                    "rejected hierarchy preserves exact Class");
            try {
                Evolving.class.getDeclaredField("added");
                throw new AssertionError("rejected schema added a field");
            } catch (NoSuchFieldException expected) { }
            int recovery = evolve("portable-body");
            report.put("recoveryCode", recovery);
            require(recovery == 0, "body evolution after failed schema is usable");
            check(instance, cached, 48);
            require(new Evolving(5).compute() == 12, "fresh object observes actual replacement");
            JSONObject after = new JSONObject(NativeBridge.capabilities());
            require(!after.optBoolean("evolutionAvailable") && !after.optBoolean("privateHooksSupported"),
                    "public replacement grants no private capability");
            report.put("recoveredValue", instance.compute()).put("jniValue", NativeTestBridge.inspectRetainedFixture()[0])
                    .put("reflectionValue", cached.invoke(instance)).put("retainedFieldValue", instance.value)
                    .put("staticValue", Evolving.counter).put("capabilitiesAfter", after)
                    .put("checks", checks).put("passed", true);
        } catch (Throwable failure) {
            try { report.put("passed", false).put("error", failure.toString()); }
            catch (Exception ignored) { }
        } finally {
            if (retained) NativeTestBridge.releaseFixture();
        }
        return report;
    }

    private int evolve(String variant) throws Exception {
        String path = variant + "/com/github/ytlog/artdcevm/fixture/Evolving.dex";
        byte[] dex;
        try (InputStream in = context.getAssets().open(path); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] bytes = new byte[8192];
            for (int n; (n = in.read(bytes)) != -1;) out.write(bytes, 0, n);
            dex = out.toByteArray();
        }
        return NativeBridge.apply(2, new Class<?>[]{Evolving.class}, new byte[][]{dex});
    }

    private void check(Evolving instance, Method cached, int expected) throws Exception {
        int[] jni = NativeTestBridge.inspectRetainedFixture();
        require(instance.compute() == expected && ((Integer) cached.invoke(instance)) == expected
                && jni.length == 2 && jni[0] == expected && jni[1] == 41,
                "actual direct/cached reflection/cached JNI dispatch");
        require(instance.value == 41 && Evolving.counter == 113,
                "existing instance and initialized static values survive");
        ++checks;
    }
    private static void require(boolean value, String what) { if (!value) throw new AssertionError(what); }
}
