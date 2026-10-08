package com.github.ytlog.artdcevm.testing;
import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONObject;
public final class RuntimeAccessInstrumentation extends Instrumentation {
    private boolean runtimeServices;
    @Override public void onCreate(Bundle args) {super.onCreate(args);runtimeServices=args!=null&&"true".equals(args.getString("runtimeServices"));start();}
    @Override public void onStart() {
        if(runtimeServices) {
            try {com.github.ytlog.artdcevm.api.DcevmRuntime.attachPublicJvmti(getTargetContext());}
            catch(Exception error) {Bundle failure=new Bundle();failure.putString("stream",error.toString());finish(0,failure);return;}
        }
        JSONObject report=new RuntimeAccessSuite().run(getTargetContext().getApplicationInfo().sourceDir,runtimeServices);
        Bundle result=new Bundle();result.putString("stream",report.toString());
        finish(report.optBoolean("passed")?-1:0,result);
    }
}
