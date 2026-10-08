package com.github.ytlog.artdcevm.testing;

import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import java.lang.reflect.Method;
import org.json.JSONArray;
import org.json.JSONObject;

/** Owned JNI access calibration; does not initialize the evolution backend. */
public final class RuntimeAccessSuite {
    private static native String artBuildId();
    private static native int probe(Object carrier,Thread current);
    public JSONObject run() {
        JSONObject report=new JSONObject();
        try {
            report.put("sdk",Build.VERSION.SDK_INT).put("fingerprint",Build.FINGERPRINT)
                    .put("abi",Build.SUPPORTED_ABIS[0])
                    .put("pageSize",Os.sysconf(OsConstants._SC_PAGESIZE))
                    .put("fullDcevmComplete",false).put("privateEvolutionInitialized",false);
            System.loadLibrary("artdcevm");
            report.put("artBuildId",artBuildId());
            Method carrier=Object.class.getMethod("toString");
            int main=probe(carrier,Thread.currentThread());
            report.put("mainCallbackHits",main);
            if(main!=128)throw new AssertionError("Owned JNI access main: "+main);
            int[] results={-1,-1};Throwable[] errors=new Throwable[2];Thread[] workers=new Thread[2];
            for(int i=0;i<2;i++) {
                final int slot=i;
                workers[i]=new Thread(() -> {
                    try {results[slot]=probe(carrier,Thread.currentThread());}
                    catch(Throwable error) {errors[slot]=error;}
                },"OwnedJniAccess"+i);
                workers[i].start();
            }
            for(Thread worker:workers)worker.join(30000);
            report.put("workerCallbackHits",new JSONArray(results));
            for(int i=0;i<2;i++) {
                if(workers[i].isAlive()||errors[i]!=null||results[i]!=128)
                    throw new AssertionError("Owned JNI access worker "+i+": "+results[i],errors[i]);
            }
            report.put("passed",true);
        } catch(Throwable error) {
            try {report.put("passed",false).put("error",android.util.Log.getStackTraceString(error));}
            catch(Exception ignored) { }
        }
        return report;
    }
}
