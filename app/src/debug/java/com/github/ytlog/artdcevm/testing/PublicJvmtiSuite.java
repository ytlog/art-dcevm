package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.testing.TestRuntimeBackend.PatchRequest;
import com.github.ytlog.artdcevm.testing.TestRuntimeBackend;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import org.json.JSONObject;

/** Portable JVMTI behavior check; runs independently of private ART hooks. */
public final class PublicJvmtiSuite {
    private final Context context;
    public PublicJvmtiSuite(Context context) { this.context = context; }

    public JSONObject run() {
        JSONObject result = new JSONObject();
        try {
            JSONObject publicCapabilities = DcevmRuntime.attachPublicJvmti(context);
            TestRuntimeBackend backend = new TestRuntimeBackend(context);
            JSONObject capabilities = backend.capabilities();
            result.put("sdk", Build.VERSION.SDK_INT).put("abi", Build.SUPPORTED_ABIS[0])
                    .put("pageSize", Os.sysconf(OsConstants._SC_PAGESIZE))
                    .put("fingerprint", Build.FINGERPRINT).put("capabilities", capabilities);
            require(capabilities.getBoolean("attached"), "agent attach");
            require(publicCapabilities.getString("backend").equals(capabilities.getString("backend")),
                    "public attach uses the selected backend");
            require(capabilities.getBoolean("methodBody"), "body capability");
            require(!capabilities.getBoolean("evolutionAvailable"), "source engine must be unavailable");
            if (backend.id().startsWith("public-jvmti")) {
                require(backend.enableEvolution().code == 98, "uncalibrated evolution refusal");
                try {
                    DcevmRuntime.initialize(context);
                    throw new AssertionError("uncalibrated full evolution initialization succeeded");
                } catch (IllegalStateException expected) {
                    require(expected.getMessage().contains("98"), "explicit unsupported initialization");
                    result.put("fullInitializationRejected", true);
                }
                require(DcevmRuntime.attachPublicJvmti(context).getBoolean("methodBody"),
                        "public attach remains usable after full initialization refusal");
            }
            Evolving instance = new Evolving(41);
            require(instance.compute() == 42, "original body");
            StandardJvmtiClient.attach(context);
            require(StandardJvmtiClient.redefine(new Class<?>[]{Evolving.class},
                    new byte[][]{fixture("body")}) == 0, "independent Agent body redefine");
            require(instance.compute() == 43, "new body and preserved state");
            int structureCode = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_STRUCTURE,
                    new Class<?>[]{Evolving.class}, new byte[][]{fixture("add")})).code;
            result.put("structureCode", structureCode);
            if (capabilities.getBoolean("nativeAdditive")) {
                require(structureCode == 0, "advertised additive redefine");
                require(Evolving.class.getField("added").getLong(instance) == 0,
                        "existing instance field default");
                Evolving.class.getField("added").setLong(instance, 9);
                int deletionCode = backend.apply(new PatchRequest(PatchRequest.Operation.REDEFINE_STRUCTURE,
                        new Class<?>[]{Evolving.class}, new byte[][]{fixture("delete")})).code;
                result.put("deletionProbeCode", deletionCode);
                if (deletionCode == 0) {
                    require(instance.compute() == 45, "deleted schema uses new method body");
                    try {
                        Evolving.class.getField("added");
                        throw new AssertionError("deleted field remains visible");
                    } catch (NoSuchFieldException expected) { }
                } else {
                    require(Evolving.class.getField("added").getLong(instance) == 9 &&
                            instance.compute() == 44, "rejected deletion leaves old schema intact");
                }
            } else {
                require(structureCode == 98, "unavailable structure is explicit");
            }
            require(backend.apply(new PatchRequest(PatchRequest.Operation.EVOLVE_SCHEMA,
                    new Class<?>[]{Evolving.class}, new byte[][]{fixture("delete")})).code == 98,
                    "unavailable evolution is explicit");
            result.put("passed", true);
        } catch (Throwable failure) {
            try { result.put("passed", false).put("error", failure.toString()); }
            catch (Exception ignored) { }
        }
        return result;
    }

    private byte[] fixture(String variant) throws Exception {
        String name = variant + "/com/github/ytlog/artdcevm/fixture/Evolving.dex";
        try (InputStream in = context.getAssets().open(name);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            for (int n; (n = in.read(buffer)) != -1;) out.write(buffer, 0, n);
            return out.toByteArray();
        }
    }

    private static void require(boolean value, String description) {
        if (!value) throw new AssertionError(description);
    }
}
