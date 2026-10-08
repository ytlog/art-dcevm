package com.github.ytlog.artdcevm.testing;

import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import java.lang.reflect.Method;
import java.lang.reflect.Executable;
import java.lang.reflect.Field;
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
            Class<?> owner,Object carrier,Thread current,int direct,int virtual,int tableCandidate);
    private static native int methodTableCount();
    private static native String probeFields(Class<?> owner,String descriptor,Object[] fields,
            String[] names,String[] types,int[] modifiers);
    private static String descriptor(Class<?> type) {
        if(type.isPrimitive()) {
            if(type==boolean.class)return "Z";if(type==byte.class)return "B";
            if(type==char.class)return "C";if(type==short.class)return "S";
            if(type==int.class)return "I";if(type==long.class)return "J";
            if(type==float.class)return "F";if(type==double.class)return "D";
            return "V";
        }
        String name=type.getName().replace('.', '/');
        return type.isArray()?name:"L"+name+";";
    }
    private static String fields() throws Exception {
        Class<?> owner=Class.forName("com.github.ytlog.artdcevm.internal.CalibrationFixture");
        Field[] fields=owner.getDeclaredFields();String[] names=new String[fields.length],types=new String[fields.length];
        int[] flags=new int[fields.length];
        for(int i=0;i<fields.length;i++){names[i]=fields[i].getName();types[i]=descriptor(fields[i].getType());flags[i]=fields[i].getModifiers();}
        if(fields.length!=28)throw new AssertionError("Fixture field oracle dimensions: "+fields.length);
        String result=probeFields(owner,descriptor(owner),fields,names,types,flags);
        if(!"split".equals(result)&&!"merged".equals(result))throw new AssertionError(result);
        flags[0]^=8;
        String rejected=probeFields(owner,descriptor(owner),fields,names,types,flags);
        if("split".equals(rejected)||"merged".equals(rejected))throw new AssertionError("Incorrect independent modifiers accepted");
        return result;
    }
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
            Executable[] methods={MethodIdentityFixture.class.getDeclaredConstructor(),MethodIdentityFixture.class.getDeclaredMethod("instanceMarker",int.class),
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
            int model=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,Build.VERSION.SDK_INT==37?1:0);
            report.put("mainMethodTableCount",methodTableCount());
            if(Build.VERSION.SDK_INT==37&&methodTableCount()!=methods.length)throw new AssertionError("Android17 method table oracle");
            report.put("mainMethodModel",model);
            if(model!=11&&model!=12)throw new AssertionError("Owned method identity main: "+model);
            int[] results={-1,-1},contexts={-1,-1},models={-1,-1},tables={-1,-1};String[] routes=new String[2];Throwable[] errors=new Throwable[2];Thread[] workers=new Thread[2];
            for(int i=0;i<2;i++) {
                final int slot=i;
                workers[i]=new Thread(() -> {
                    try {
                        results[slot]=probe(carrier,Thread.currentThread());routes[slot]=probeRoute();contexts[slot]=probeContext();
                        models[slot]=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,Build.VERSION.SDK_INT==37?1:0);
                        tables[slot]=methodTableCount();
                        if(Build.VERSION.SDK_INT==37&&tables[slot]!=methods.length)throw new AssertionError("Worker method table oracle");
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
            report.put("workerMethodTableCounts",new JSONArray(tables));
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
            report.put("fieldStorageModel",fields());
            report.put("fieldIdentityCount",28);
            report.put("passed",true);
        } catch(Throwable error) {
            try {report.put("passed",false).put("error",android.util.Log.getStackTraceString(error));}
            catch(Exception ignored) { }
        }
        return report;
    }
}
