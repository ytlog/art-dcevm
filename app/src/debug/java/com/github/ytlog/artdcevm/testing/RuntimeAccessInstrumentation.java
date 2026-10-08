package com.github.ytlog.artdcevm.testing;
import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONObject;
public final class RuntimeAccessInstrumentation extends Instrumentation {
    private boolean runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage;
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        runtimeServices=enabled(args,"runtimeServices");exclusiveRoots=enabled(args,"exclusiveRoots");
        nativeCache=enabled(args,"nativeCache");jitReceivers=enabled(args,"jitReceivers");
        methodGuards=enabled(args,"methodGuards");fieldStorage=enabled(args,"fieldStorage");start();
    }
    private static boolean enabled(Bundle args,String key){return args!=null&&"true".equals(args.getString(key));}
    @Override public void onStart() {
        if(runtimeServices||nativeCache||jitReceivers||methodGuards) {
            try {com.github.ytlog.artdcevm.api.DcevmRuntime.attachPublicJvmti(getTargetContext());}
            catch(Exception error) {Bundle failure=new Bundle();failure.putString("stream",error.toString());finish(0,failure);return;}
        }
        JSONObject report=new RuntimeAccessSuite().run(getTargetContext().getApplicationInfo().sourceDir,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage);
        Bundle result=new Bundle();result.putString("stream",report.toString());
        finish(report.optBoolean("passed")?-1:0,result);
    }
}
