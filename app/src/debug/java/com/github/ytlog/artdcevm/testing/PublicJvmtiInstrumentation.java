package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONObject;

public final class PublicJvmtiInstrumentation extends Instrumentation {
    private boolean mvpBody;
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments);
        mvpBody = arguments != null && "mvp-body".equals(arguments.getString("suite"));
        start(); }
    @Override public void onStart() {
        JSONObject report = mvpBody ? new MvpBodyEvolutionSuite(getTargetContext()).run()
                : new PublicJvmtiSuite(getTargetContext()).run();
        Bundle output = new Bundle();
        output.putString("stream", report.toString());
        finish(report.optBoolean("passed") ? -1 : 0, output);
    }
}
