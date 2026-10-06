package dev.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONObject;

public final class PublicJvmtiInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    @Override public void onStart() {
        JSONObject report = new PublicJvmtiSuite(getTargetContext()).run();
        Bundle output = new Bundle();
        output.putString("stream", report.toString());
        finish(report.optBoolean("passed") ? -1 : 0, output);
    }
}
