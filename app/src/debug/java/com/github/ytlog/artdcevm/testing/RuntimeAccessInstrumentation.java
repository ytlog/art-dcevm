package com.github.ytlog.artdcevm.testing;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import org.json.JSONObject;
public final class RuntimeAccessInstrumentation extends Instrumentation {
    private boolean runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage,foreground;
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        runtimeServices=enabled(args,"runtimeServices");exclusiveRoots=enabled(args,"exclusiveRoots");
        nativeCache=enabled(args,"nativeCache");jitReceivers=enabled(args,"jitReceivers");
        methodGuards=enabled(args,"methodGuards");fieldStorage=enabled(args,"fieldStorage");
        foreground=enabled(args,"foreground");start();
    }
    private static boolean enabled(Bundle args,String key){return args!=null&&"true".equals(args.getString(key));}
    private static void put(JSONObject report,String key,Object value) {
        try { report.put(key,value); } catch(org.json.JSONException impossible) { throw new IllegalStateException(impossible); }
    }
    @Override public void onStart() {
        JSONObject report=new JSONObject();
        RuntimeAccessForegroundActivity carrier=null;
        boolean before=false;int pauses=0;
        try {
            if(foreground) {
                Intent intent=new Intent(getTargetContext(),RuntimeAccessForegroundActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                carrier=(RuntimeAccessForegroundActivity)startActivitySync(intent);
                waitForIdleSync();
                before=carrier.carrierResumed();pauses=carrier.carrierPauses();
                if(!before||carrier.carrierPid()!=Process.myPid())
                    throw new IllegalStateException("Foreground carrier did not resume in the instrumentation process");
            }
            if(runtimeServices||nativeCache||jitReceivers||methodGuards)
                com.github.ytlog.artdcevm.api.DcevmRuntime.attachPublicJvmti(getTargetContext());
            report=new RuntimeAccessSuite().run(getTargetContext().getApplicationInfo().sourceDir,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage);
        } catch(Throwable error) {
            put(report,"passed",false);put(report,"error",android.util.Log.getStackTraceString(error));
        } finally {
            put(report,"foregroundRequested",foreground);
            if(foreground) {
                boolean after=carrier!=null&&carrier.carrierResumed();
                int observedPauses=carrier==null?-1:carrier.carrierPauses()-pauses;
                put(report,"foregroundActivity",RuntimeAccessForegroundActivity.class.getName());
                put(report,"foregroundInstrumentationPid",Process.myPid());
                put(report,"foregroundActivityPid",carrier==null?0:carrier.carrierPid());
                put(report,"foregroundResumedBefore",before);put(report,"foregroundResumedAfter",after);
                put(report,"foregroundPauseCount",observedPauses);
                boolean finished=false;
                if(carrier!=null) {
                    final RuntimeAccessForegroundActivity closing=carrier;
                    try { runOnMainSync(closing::finish);finished=closing.awaitDestroyed(); }
                    catch(Throwable error) { put(report,"foregroundCleanupError",android.util.Log.getStackTraceString(error)); }
                }
                put(report,"foregroundFinished",finished);
                if(!before||!after||observedPauses!=0||!finished) {
                    put(report,"passed",false);
                    if(!report.has("error"))put(report,"error","Foreground carrier failed its lifecycle proof or cleanup");
                }
            }
        }
        Bundle result=new Bundle();result.putString("stream",report.toString());
        finish(report.optBoolean("passed")?-1:0,result);
    }
}
