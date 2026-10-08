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
    private static native String probeRoute();
    private static native int probeContext();
    private static native String probeRuntimeServices();
    private static native int probeMethods(Object[] methods,int[] indices,int[] modifiers,
            Class<?> owner,Object carrier,Thread current);
    public JSONObject run(String apkPath) { return run(apkPath,false); }
    public JSONObject run(String apkPath,boolean runtimeServices) {
        JSONObject report=new JSONObject();
        try {
            report.put("sdk",Build.VERSION.SDK_INT).put("fingerprint",Build.FINGERPRINT)
                    .put("abi",Build.SUPPORTED_ABIS[0])
                    .put("pageSize",Os.sysconf(OsConstants._SC_PAGESIZE))
                    .put("fullDcevmComplete",false).put("privateEvolutionInitialized",false);
            System.loadLibrary("artdcevm");
            report.put("artBuildId",artBuildId());
            Method carrier=Object.class.getMethod("toString");
            Method[] methods={MethodIdentityFixture.class.getDeclaredMethod("instanceMarker",int.class),
                    MethodIdentityFixture.class.getDeclaredMethod("staticMarker",long.class),
                    MethodIdentityFixture.class.getDeclaredMethod("nativeMarker")};
            MethodDexOracle.Result oracle=MethodDexOracle.read(apkPath,methods);
            report.put("methodDexEntry",oracle.dexEntry).put("methodDexHeaderOffset",oracle.headerOffset)
                    .put("methodDexIndices",new JSONArray(oracle.indices));
            int main=probe(carrier,Thread.currentThread());
            report.put("mainCallbackHits",main);
            report.put("mainRoute",probeRoute());
            if(main!=128)throw new AssertionError("Owned JNI access main: "+main);
            int context=probeContext();report.put("mainContextChecks",context);
            if(context!=384)throw new AssertionError("Owned Context main: "+context);
            int model=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread());
            report.put("mainMethodModel",model);
            if(model!=11&&model!=12)throw new AssertionError("Owned method identity main: "+model);
            int[] results={-1,-1},contexts={-1,-1},models={-1,-1};String[] routes=new String[2];Throwable[] errors=new Throwable[2];Thread[] workers=new Thread[2];
            for(int i=0;i<2;i++) {
                final int slot=i;
                workers[i]=new Thread(() -> {
                    try {
                        results[slot]=probe(carrier,Thread.currentThread());routes[slot]=probeRoute();contexts[slot]=probeContext();
                        models[slot]=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread());
                    }
                    catch(Throwable error) {errors[slot]=error;}
                },"OwnedJniAccess"+i);
                workers[i].start();
            }
            for(Thread worker:workers)worker.join(30000);
            report.put("workerCallbackHits",new JSONArray(results));
            report.put("workerRoutes",new JSONArray(routes));
            report.put("workerContextChecks",new JSONArray(contexts));
            report.put("workerMethodModels",new JSONArray(models));
            for(int i=0;i<2;i++) {
                if(workers[i].isAlive()||errors[i]!=null||results[i]!=128||contexts[i]!=384||models[i]!=model)
                    throw new AssertionError("Owned JNI access worker "+i+": "+results[i],errors[i]);
            }
            if(runtimeServices) {
                String serviceError=probeRuntimeServices();
                report.put("runtimeServicesError",serviceError);
                report.put("runtimeServicesCalibrated",serviceError.isEmpty());
                if(!serviceError.isEmpty())throw new AssertionError(serviceError);
            }
            report.put("passed",true);
        } catch(Throwable error) {
            try {report.put("passed",false).put("error",android.util.Log.getStackTraceString(error));}
            catch(Exception ignored) { }
        }
        return report;
    }
}
