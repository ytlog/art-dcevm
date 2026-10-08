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
    private static native int probeCacheThreads();
    private static native String probeNativeCache();
    private static native int probeCardOffset();
    private static native String probeMethodGuards(Class<?> owner);
    private static native String probeFieldGuards(Class<?> owner,Object instance,Field instanceField,Field staticField);
    private static native int probeMethods(Object[] methods,int[] indices,int[] modifiers,
            Class<?> owner,Object carrier,Thread current,int direct,int virtual,int tableCandidate);
    private static native int methodTableCount();
    private static native String probeExclusive(Class<?> owner,int iterations);
    private static native String probeJitServices(Object hot,Class<?> owner,int index,int modifiers,Class<?> target,byte[] dex);
    private static native String probeFields(Class<?> owner,String descriptor,Object[] fields,
            String[] names,String[] types,int[] modifiers,boolean storage);
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
    private static String fields(boolean storage) throws Exception {
        Class<?> owner=Class.forName("com.github.ytlog.artdcevm.internal.CalibrationFixture");
        Field[] fields=owner.getDeclaredFields();String[] names=new String[fields.length],types=new String[fields.length];
        int[] flags=new int[fields.length];
        for(int i=0;i<fields.length;i++){names[i]=fields[i].getName();types[i]=descriptor(fields[i].getType());flags[i]=fields[i].getModifiers();}
        if(fields.length!=28)throw new AssertionError("Fixture field oracle dimensions: "+fields.length);
        String result=probeFields(owner,descriptor(owner),fields,names,types,flags,storage);
        if(!"split".equals(result)&&!"merged".equals(result))throw new AssertionError(result);
        flags[0]^=8;
        String rejected=probeFields(owner,descriptor(owner),fields,names,types,flags,storage);
        if("split".equals(rejected)||"merged".equals(rejected))throw new AssertionError("Incorrect independent modifiers accepted");
        return result;
    }
    public JSONObject run(String apkPath) { return run(apkPath,false); }
    public JSONObject run(String apkPath,boolean runtimeServices) { return run(apkPath,runtimeServices,false,false,false,false,false); }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers) {
        return run(apkPath,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,false,false);
    }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers,boolean methodGuards,boolean fieldStorage) {
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
                    MethodIdentityFixture.class.getDeclaredMethod("nativeMarker"),
                    MethodIdentityFixture.class.getDeclaredMethod("hotLoop",int.class)};
            MethodDexOracle.Result oracle=MethodDexOracle.read(apkPath,methods);
            report.put("methodDexEntry",oracle.dexEntry).put("methodDexHeaderOffset",oracle.headerOffset)
                    .put("methodDexIndices",new JSONArray(oracle.indices))
                    .put("methodDexDirectCount",oracle.directMethods).put("methodDexVirtualCount",oracle.virtualMethods);
            int main=probe(carrier,Thread.currentThread());
            report.put("mainCallbackHits",main);
            report.put("mainRoute",probeRoute());
            if(main!=128)throw new AssertionError("Owned JNI access main: "+main);
            int context=probeContext();report.put("mainContextChecks",context);
            if(context!=384)throw new AssertionError("Owned Context main: "+context);
            int model=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,1);
            report.put("mainMethodTableCount",methodTableCount());
            if(methodTableCount()!=methods.length)throw new AssertionError("Complete method table oracle");
            report.put("mainMethodModel",model).put("executableMirrorIdentityCount",methods.length).put("registeredNativeBindingCalibrated",true);
            if(model!=11&&model!=12)throw new AssertionError("Owned method identity main: "+model);
            if(runtimeServices||nativeCache||jitReceivers||methodGuards) {
                String serviceError=probeRuntimeServices();
                report.put("runtimeServicesError",serviceError).put("runtimeServicesCalibrated",serviceError!=null&&serviceError.isEmpty());
                if(serviceError==null||!serviceError.isEmpty())throw new AssertionError(serviceError);
            }
            if(runtimeServices||nativeCache||jitReceivers||methodGuards) {
                int commitModel=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,
                        carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,1);
                report.put("mainCommitMethodModel",commitModel);
                if(commitModel!=model)throw new AssertionError("Owned modern method commit service: "+commitModel);
            }
            if(jitReceivers) {
                byte[] additive;
                try(java.util.zip.ZipFile zip=new java.util.zip.ZipFile(apkPath);
                    java.io.InputStream in=zip.getInputStream(zip.getEntry("assets/add/com/github/ytlog/artdcevm/fixture/Evolving.dex"))) {
                    java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[8192];int n;
                    while((n=in.read(chunk))!=-1)bytes.write(chunk,0,n);additive=bytes.toByteArray();
                }
                String jit=probeJitServices(methods[4],MethodIdentityFixture.class,oracle.indices[4],oracle.modifiers[4],
                        com.github.ytlog.artdcevm.fixture.Evolving.class,additive);
                report.put("jitServices",jit);
                if(!jit.equals("jit,code-cache,heap"))throw new AssertionError(jit);
            }
            int[] results={-1,-1},contexts={-1,-1},models={-1,-1},tables={-1,-1};String[] routes=new String[2];Throwable[] errors=new Throwable[2];Thread[] workers=new Thread[2];
            for(int i=0;i<2;i++) {
                final int slot=i;
                workers[i]=new Thread(() -> {
                    try {
                        results[slot]=probe(carrier,Thread.currentThread());routes[slot]=probeRoute();contexts[slot]=probeContext();
                        models[slot]=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,1);
                        tables[slot]=methodTableCount();
                        if(tables[slot]!=methods.length)throw new AssertionError("Worker method table oracle");
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
            report.put("fieldStorageModel",fields(fieldStorage));
            if(fieldStorage)report.put("nativeFieldStorageCalibrated",true).put("nativeCardTableOffset",probeCardOffset());
            report.put("fieldIdentityCount",28);
            if(methodGuards) {
                String guardError=probeMethodGuards(RetiredEntryFixture.class);
                report.put("methodEntryGuardError",guardError);
                if(!guardError.isEmpty())throw new AssertionError(guardError);
                boolean reflectedRejected=false;
                try {RetiredEntryFixture.class.getDeclaredMethod("removed").invoke(null);}
                catch(NoSuchMethodError expected) {reflectedRejected=true;}
                catch(java.lang.reflect.InvocationTargetException wrapped) {
                    if(wrapped.getCause() instanceof NoSuchMethodError)reflectedRejected=true;
                    else throw wrapped;
                }
                if(!reflectedRejected)throw new AssertionError("Retired reflection invoke accepted");
                if(((Integer)RetiredEntryFixture.class.getDeclaredMethod("kept").invoke(null))!=73)
                    throw new AssertionError("Unretired reflection invoke changed");
                report.put("methodEntryGuardsCalibrated",true);
                RetiredEntryFixture fieldFixture=new RetiredEntryFixture();
                Field removedField=RetiredEntryFixture.class.getDeclaredField("removedField");
                Field removedStatic=RetiredEntryFixture.class.getDeclaredField("removedStatic");
                String fieldGuardError=probeFieldGuards(RetiredEntryFixture.class,fieldFixture,removedField,removedStatic);
                report.put("fieldEntryGuardError",fieldGuardError);
                if(!fieldGuardError.isEmpty())throw new AssertionError(fieldGuardError);
                boolean getRejected=false,setRejected=false,staticRejected=false;
                try {removedField.getInt(fieldFixture);}catch(NoSuchFieldError expected){getRejected=true;}
                try {removedField.setInt(fieldFixture,99);}catch(NoSuchFieldError expected){setRejected=true;}
                try {removedStatic.getLong(null);}catch(NoSuchFieldError expected){staticRejected=true;}
                if(!getRejected||!setRejected||!staticRejected)throw new AssertionError("Retired Field reflection access accepted");
                if(RetiredEntryFixture.class.getDeclaredField("keptField").getInt(fieldFixture)!=73||
                        RetiredEntryFixture.class.getDeclaredField("keptStatic").getLong(null)!=17)
                    throw new AssertionError("Unretired Field reflection changed");
                report.put("fieldEntryGuardsCalibrated",true);
            }
            if(exclusiveRoots) {
                java.util.concurrent.atomic.AtomicBoolean gcRunning=new java.util.concurrent.atomic.AtomicBoolean(true);
                long gcBefore=Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count"));
                java.util.concurrent.atomic.AtomicInteger gcCycles=new java.util.concurrent.atomic.AtomicInteger();
                Thread gc=new Thread(() -> {byte[][] pressure=new byte[16][];int slot=0;while(gcRunning.get()){pressure[slot++&15]=new byte[65536];System.gc();gcCycles.incrementAndGet();Thread.yield();}},"OwnedExclusiveGC");
                gc.start();String exclusiveError;
                try {exclusiveError=probeExclusive(Class.forName("com.github.ytlog.artdcevm.internal.CalibrationFixture"),32);}
                finally {gcRunning.set(false);gc.join(30000);}
                long gcAfter=Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count"));
                if(gc.isAlive()||gcCycles.get()<1||gcAfter<=gcBefore||exclusiveError==null||!exclusiveError.isEmpty())throw new AssertionError("Exclusive monitor/GC: "+exclusiveError);
                report.put("exclusiveMonitorIterations",32).put("exclusiveFailureRestoration",true).put("exclusiveGcRequests",gcCycles.get()).put("exclusiveGcCollections",gcAfter-gcBefore);
            }
            if(nativeCache) {
                String cacheError=probeNativeCache();int cacheThreads=probeCacheThreads();
                report.put("nativeCacheError",cacheError).put("nativeCacheClearedThreads",cacheThreads);
                if(cacheError==null||!cacheError.isEmpty()||cacheThreads<=0)
                    throw new AssertionError("Native cache clearing: "+cacheError);
            }
            report.put("passed",true);
        } catch(Throwable error) {
            try {report.put("passed",false).put("error",android.util.Log.getStackTraceString(error));}
            catch(Exception ignored) { }
        }
        return report;
    }
}
