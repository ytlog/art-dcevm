package com.github.ytlog.artdcevm.testing;
import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONObject;
public final class RuntimeAccessInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args) {super.onCreate(args);start();}
    @Override public void onStart() {
        JSONObject report=new RuntimeAccessSuite().run();
        Bundle result=new Bundle();result.putString("stream",report.toString());
        finish(report.optBoolean("passed")?-1:0,result);
    }
}
