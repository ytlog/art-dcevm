package dev.artdcevm;
import android.app.Instrumentation;
import android.os.Bundle;
import dev.artdcevm.testing.ConformanceSuite;
import org.json.JSONObject;
public final class ConformanceInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    @Override public void onStart() {
        JSONObject report = new ConformanceSuite(getTargetContext()).run();
        Bundle result = new Bundle();
        result.putString("stream", report.toString());
        finish(report.optInt("failures", 1) == 0 ? -1 : 0, result);
    }
}
