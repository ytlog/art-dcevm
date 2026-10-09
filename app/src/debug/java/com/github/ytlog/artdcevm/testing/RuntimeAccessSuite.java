package com.github.ytlog.artdcevm.testing;

import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import java.lang.reflect.Method;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
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
    private static native String probeOwnedMethodDeletion(Class<?> owner,Object instance,byte[] dex,MethodHandle[] handles,int baseline);
    private static native long probeOwnedCommitDiagnostics();
    private static int invokeOwnedMethodHandle(MethodHandle handle,Object instance) throws Throwable {
        return (int)handle.invokeExact((com.github.ytlog.artdcevm.fixture.Evolving)instance);
    }

    private static native String probeOwnedFieldDeletion(Class<?> owner,Object instance,byte[] dex);
    private static native String prepareOwnedFieldDeletionGuards(Class<?> owner,Object instanceHandle,Object staticHandle,Field instanceField,Field staticField,MethodHandle instanceGetter,MethodHandle staticGetter);
    private static native int probeCacheThreads();
    private static native int probeOwnedJitMethodState(Method method);
    private static native String probeNativeCache();
    private static native int probeCardOffset();
    private static native String probeMethodGuards(Class<?> owner);
    private static native String prepareFieldGuards(Class<?> owner,Object instance,Field instanceField,Field staticField,MethodHandle instanceGetter,MethodHandle staticGetter);
    private static native String probeFieldGuards(Class<?> owner,Object instance,Field instanceField,Field staticField);
    private static native String probeAllocatorCleanup(Class<?> owner,ClassLoader loader,Field[] fields,Executable[] methods,Method bootMethod);
    private static native int[] cleanupCounts();
    private static java.lang.ref.WeakReference<ClassLoader> allocatorFixture(String apkPath) throws Exception {
        byte[] dex;
        try(java.util.zip.ZipFile apk=new java.util.zip.ZipFile(apkPath)) {
            java.util.zip.ZipEntry entry=apk.getEntry("assets/field-original/com/github/ytlog/artdcevm/fixture/FieldSubject.dex");
            if(entry==null)throw new AssertionError("Missing isolated loader fixture DEX");
            try(java.io.InputStream in=apk.getInputStream(entry);java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream()) {
                byte[] block=new byte[4096];int count;while((count=in.read(block))!=-1)out.write(block,0,count);dex=out.toByteArray();
            }
        }
        ClassLoader loader=new dalvik.system.InMemoryDexClassLoader(java.nio.ByteBuffer.wrap(dex),null);
        Class<?> owner=Class.forName("com.github.ytlog.artdcevm.fixture.FieldSubject",true,loader);
        if(owner.getClassLoader()!=loader)throw new AssertionError("Fixture delegated to another loader");
        Field[] fields=owner.getDeclaredFields();Method[] methods=owner.getDeclaredMethods();
        java.lang.reflect.Constructor<?>[] constructors=owner.getDeclaredConstructors();
        Executable[] members=new Executable[methods.length+constructors.length];
        System.arraycopy(methods,0,members,0,methods.length);System.arraycopy(constructors,0,members,methods.length,constructors.length);
        String error=probeAllocatorCleanup(owner,loader,fields,members,Object.class.getMethod("toString"));
        if(error==null||!error.isEmpty())throw new AssertionError(error);
        return new java.lang.ref.WeakReference<ClassLoader>(loader);
    }
    private static native int probeMethods(Object[] methods,int[] indices,int[] modifiers,
            Class<?> owner,Object carrier,Thread current,int direct,int virtual,int tableCandidate);
    private static native int methodTableCount();
    private static native String probeExclusive(Class<?> owner,int iterations);
    private static native String probeJitServices(Object hot,Class<?> owner,int index,int modifiers,Class<?> target,byte[] dex);
    private static native String probeFields(Class<?> owner,String descriptor,Object[] fields,
            String[] names,String[] types,int[] modifiers,boolean storage);
    private interface FieldAccess { void run() throws Throwable; }
    private static native String beginFieldHandleProof(MethodHandle getter,Field field);
    private static native String endFieldHandleProof(boolean invocationSucceeded);
    private static native void releaseFieldProofStages();
    private static void proveFieldHandle(MethodHandle getter,Field field,FieldAccess access) throws Throwable {
        String before=beginFieldHandleProof(getter,field);
        if(before==null||!before.isEmpty())throw new AssertionError("Field handle native proof: "+before);
        boolean succeeded=false;
        try {access.run();succeeded=true;}
        finally {
            String after=endFieldHandleProof(succeeded);
            if(succeeded&&(after==null||!after.isEmpty()))throw new AssertionError("Actual original field handle dispatch: "+after);
        }
    }
    private static void expectRemovedField(FieldAccess access) throws Throwable {
        try {access.run();} catch(NoSuchFieldError expected) {return;}
        catch(java.lang.reflect.InvocationTargetException wrapped) {if(wrapped.getCause() instanceof NoSuchFieldError)return;throw wrapped;}
        throw new AssertionError("Cached removed field access succeeded");
    }
    private static void collectAndVerifyGc(JSONObject report,String prefix) throws Exception {
        long before=Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count"));
        long after=before;
        for(int attempt=0;attempt<20&&after<=before;attempt++) {
            System.gc();System.runFinalization();Thread.sleep(10);
            after=Long.parseLong(android.os.Debug.getRuntimeStat("art.gc.gc-count"));
        }
        if(after<=before)throw new AssertionError("No completed GC observed before cached access: "+prefix);
        report.put(prefix+"GcCollections",after-before);
    }
    private static byte[] fixtureDex(String apkPath,String path) throws Exception {
        try(java.util.zip.ZipFile apk=new java.util.zip.ZipFile(apkPath)) {
            java.util.zip.ZipEntry entry=apk.getEntry("assets/"+path);
            if(entry==null)throw new AssertionError("Missing transaction fixture: "+path);
            try(java.io.InputStream in=apk.getInputStream(entry);java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream()) {
                byte[] block=new byte[8192];int count;while((count=in.read(block))!=-1)out.write(block,0,count);return out.toByteArray();
            }
        }
    }
    private static void ownedFieldDeletion(String apkPath,JSONObject report,TestPhaseLog phases) throws Throwable {
        com.github.ytlog.artdcevm.fixture.FieldSubject instance=new com.github.ytlog.artdcevm.fixture.FieldSubject();
        Class<?> owner=instance.getClass();int identity=System.identityHashCode(instance);
        Field removed=owner.getDeclaredField("i"),removedStatic=owner.getDeclaredField("si"),kept=owner.getDeclaredField("keep");
        java.lang.invoke.MethodHandles.Lookup lookup=java.lang.invoke.MethodHandles.lookup();
        java.lang.invoke.MethodHandle getter=lookup.unreflectGetter(removed),setter=lookup.unreflectSetter(removed),
                staticGetter=lookup.unreflectGetter(removedStatic),staticSetter=lookup.unreflectSetter(removedStatic),keptGetter=lookup.unreflectGetter(kept);
        java.lang.invoke.VarHandle variable=lookup.findVarHandle(owner,"i",int.class),staticVariable=lookup.findStaticVarHandle(owner,"si",int.class),
                keptVariable=lookup.findVarHandle(owner,"keep",int.class);
        String[] names={"z","b","c","s","i","j","f","d","o","sz","sb","sc","ss","si","sj","sf","sd","so"};
        Field[] mirrors=new Field[names.length];java.lang.invoke.MethodHandle[] getters=new java.lang.invoke.MethodHandle[names.length],setters=new java.lang.invoke.MethodHandle[names.length];
        java.lang.invoke.VarHandle[] variables=new java.lang.invoke.VarHandle[names.length];Object[] values=new Object[names.length];
        for(int i=0;i<names.length;i++) {
            mirrors[i]=owner.getDeclaredField(names[i]);values[i]=mirrors[i].get(instance);
            getters[i]=lookup.unreflectGetter(mirrors[i]);setters[i]=lookup.unreflectSetter(mirrors[i]);
            variables[i]=i<9?lookup.findVarHandle(owner,names[i],mirrors[i].getType()):lookup.findStaticVarHandle(owner,names[i],mirrors[i].getType());
        }
        String guardError=prepareOwnedFieldDeletionGuards(owner,variable,staticVariable,removed,removedStatic,getter,staticGetter);
        report.put("ownedFieldDeletionGuardPreparationError",guardError);
        if(guardError==null||!guardError.isEmpty())throw new AssertionError("Field deletion guard preparation: "+guardError);
        proveFieldHandle(getter,removed,() -> {if((int)getter.invokeExact(instance)!=7)throw new AssertionError("Instance field handle initial value");});
        proveFieldHandle(staticGetter,removedStatic,() -> {if((int)staticGetter.invokeExact()!=7)throw new AssertionError("Static field handle initial value");});
        if((int)variable.get(instance)!=7||(int)staticVariable.get()!=7)
            throw new AssertionError("Field deletion fixture initial values");
        ClassLoader callerLoader=new dalvik.system.InMemoryDexClassLoader(java.nio.ByteBuffer.wrap(
                fixtureDex(apkPath,"field-original/com/github/ytlog/artdcevm/fixture/FieldCaller.dex")),owner.getClassLoader());
        Class<?> caller=callerLoader.loadClass("com.github.ytlog.artdcevm.fixture.FieldCaller");
        Method hot=caller.getDeclaredMethod("hot",owner),cold=caller.getDeclaredMethod("cold",owner),directSet=caller.getDeclaredMethod("set",owner),
                staticGet=caller.getDeclaredMethod("staticGet"),staticSet=caller.getDeclaredMethod("staticSet");
        int compiled=0,warmCalls=0;
        for(int attempt=0;attempt<10&&compiled!=2;attempt++) {
            for(int call=0;call<100000;call++)if((Long)hot.invoke(null,instance)!=9_000_000_000L)throw new AssertionError("Hot direct field initial value");
            warmCalls+=100000;Thread.sleep(100);compiled=probeOwnedJitMethodState(hot);
        }
        report.put("ownedFieldDeletionHotCallerJitStateBefore",compiled).put("ownedFieldDeletionHotCallerWarmCalls",warmCalls);
        if(compiled!=2)throw new AssertionError("Direct field caller did not actually enter the observed ART JIT cache");
        phases.mark("field-deletion-native.before");
        String error=probeOwnedFieldDeletion(owner,instance,fixtureDex(apkPath,"field-delete/com/github/ytlog/artdcevm/fixture/FieldSubject.dex"));
        phases.mark("field-deletion-native.after");
        report.put("ownedFieldDeletionError",error);
        if(error==null||!error.isEmpty())throw new AssertionError("Owned field deletion: "+error);
        long diagnostics=probeOwnedCommitDiagnostics();
        if((diagnostics&0xffffffffL)==0||(diagnostics&(1L<<32))==0)throw new AssertionError("Field deletion native final cache/root readback missing");
        for(String name:names) {
            try {instance.getClass().getDeclaredField(name);throw new AssertionError("Deleted field remains declared: "+name);}
            catch(NoSuchFieldException expected) {}
        }
        for(int i=0;i<names.length;i++) {
            final int slot=i;Field mirror=mirrors[i];Object value=values[i];
            expectRemovedField(() -> mirror.get(instance));expectRemovedField(() -> mirror.set(instance,value));
            expectRemovedField(() -> {if(slot<9)getters[slot].invokeWithArguments(instance);else getters[slot].invokeWithArguments();});
            expectRemovedField(() -> {if(slot<9)setters[slot].invokeWithArguments(instance,value);else setters[slot].invokeWithArguments(value);});
            expectRemovedField(() -> {Object result=slot<9?variables[slot].get(instance):variables[slot].get();});
            expectRemovedField(() -> {if(slot<9)variables[slot].set(instance,value);else variables[slot].set(value);});
            expectRemovedField(() -> {if(slot<9)variables[slot].compareAndSet(instance,value,value);else variables[slot].compareAndSet(value,value);});
        }
        expectRemovedField(() -> {removed.getInt(instance);});expectRemovedField(() -> removed.setInt(instance,99));
        expectRemovedField(() -> {removedStatic.getInt(null);});expectRemovedField(() -> removedStatic.setInt(null,99));
        expectRemovedField(() -> {int value=(int)getter.invokeExact(instance);});expectRemovedField(() -> {setter.invokeExact(instance,99);});
        expectRemovedField(() -> {int value=(int)staticGetter.invokeExact();});expectRemovedField(() -> {staticSetter.invokeExact(99);});
        expectRemovedField(() -> {int value=(int)getter.invoke(instance);});expectRemovedField(() -> {setter.invoke(instance,99);});
        expectRemovedField(() -> {int value=(int)staticGetter.invoke();});expectRemovedField(() -> {staticSetter.invoke(99);});
        expectRemovedField(() -> getter.invokeWithArguments(instance));expectRemovedField(() -> setter.invokeWithArguments(instance,99));
        expectRemovedField(() -> staticGetter.invokeWithArguments());expectRemovedField(() -> staticSetter.invokeWithArguments(99));
        expectRemovedField(() -> {int value=(int)variable.get(instance);});expectRemovedField(() -> variable.set(instance,99));
        expectRemovedField(() -> {int value=(int)variable.getVolatile(instance);});expectRemovedField(() -> variable.setVolatile(instance,99));
        expectRemovedField(() -> {boolean value=variable.compareAndSet(instance,7,99);});
        expectRemovedField(() -> {int value=(int)variable.compareAndExchange(instance,7,99);});
        expectRemovedField(() -> {int value=(int)variable.getAndAdd(instance,1);});expectRemovedField(() -> {int value=(int)variable.getAndSet(instance,99);});
        expectRemovedField(() -> {int value=(int)staticVariable.get();});expectRemovedField(() -> staticVariable.set(99));
        expectRemovedField(() -> {int value=(int)staticVariable.getVolatile();});expectRemovedField(() -> staticVariable.setVolatile(99));
        expectRemovedField(() -> {boolean value=staticVariable.compareAndSet(7,99);});
        expectRemovedField(() -> {int value=(int)staticVariable.compareAndExchange(7,99);});
        expectRemovedField(() -> {int value=(int)staticVariable.getAndAdd(1);});expectRemovedField(() -> {int value=(int)staticVariable.getAndSet(99);});
        expectRemovedField(() -> hot.invoke(null,instance));expectRemovedField(() -> cold.invoke(null,instance));
        expectRemovedField(() -> directSet.invoke(null,instance));expectRemovedField(() -> staticGet.invoke(null));expectRemovedField(() -> staticSet.invoke(null));
        collectAndVerifyGc(report,"ownedFieldDeletion");expectRemovedField(() -> removed.getInt(instance));expectRemovedField(() -> {int value=(int)variable.get(instance);});
        expectRemovedField(() -> {int value=(int)getter.invokeExact(instance);});
        if(instance.keep!=41||com.github.ytlog.artdcevm.fixture.FieldSubject.staticKeep!=42||instance.compute()!=42||
                (int)keptGetter.invokeExact(instance)!=41||(int)keptVariable.get(instance)!=41||System.identityHashCode(instance)!=identity)
            throw new AssertionError("Field deletion corrupted retained values/identity/handles");
        keptVariable.set(instance,49);if(instance.compute()!=50)throw new AssertionError("Kept VarHandle write failed");keptVariable.set(instance,41);
        report.put("ownedFieldDeletion",true).put("ownedFieldDeletionJniRejected",true)
                .put("ownedFieldDeletionReflectionRejected",true).put("ownedFieldDeletionMethodHandleRejected",true)
                .put("ownedFieldDeletionVarHandleRejected",true).put("ownedFieldDeletionIdentityPreserved",true)
                .put("ownedFieldDeletionKeptInstanceValue",instance.keep).put("ownedFieldDeletionKeptStaticValue",com.github.ytlog.artdcevm.fixture.FieldSubject.staticKeep)
                .put("ownedFieldDeletionJniChecks",4).put("ownedFieldDeletionReflectionChecks",40)
                .put("ownedFieldDeletionMethodHandleChecks",48).put("ownedFieldDeletionVarHandleChecks",70)
                .put("ownedFieldDeletionDeclaringRootsRestored",true).put("ownedFieldDeletionCacheClearedThreads",diagnostics&0xffffffffL)
                .put("ownedFieldDeletionRemovedFields",18).put("ownedFieldDeletionKeptValue",instance.compute())
                .put("ownedFieldDeletionDirectChecks",5).put("ownedFieldDeletionAfterGcChecks",3);
    }
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
        return run(apkPath,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage,false);
    }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers,boolean methodGuards,boolean fieldStorage,boolean activeFrames) {
        return run(apkPath,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage,activeFrames,new TestPhaseLog(java.util.UUID.randomUUID().toString().replace("-","")));
    }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers,boolean methodGuards,boolean fieldStorage,TestPhaseLog phases) {
        return run(apkPath,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage,false,phases);
    }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers,boolean methodGuards,boolean fieldStorage,boolean activeFrames,TestPhaseLog phases) {
        return run(apkPath,runtimeServices,exclusiveRoots,nativeCache,jitReceivers,methodGuards,fieldStorage,activeFrames,false,phases);
    }
    public JSONObject run(String apkPath,boolean runtimeServices,boolean exclusiveRoots,boolean nativeCache,boolean jitReceivers,boolean methodGuards,boolean fieldStorage,boolean activeFrames,boolean physicalActiveFrames,TestPhaseLog phases) {
        JSONObject report=new JSONObject();
        boolean nativeLoaded=false;
        try {
            report.put("sdk",Build.VERSION.SDK_INT).put("fingerprint",Build.FINGERPRINT)
                    .put("abi",Build.SUPPORTED_ABIS[0])
                    .put("pageSize",Os.sysconf(OsConstants._SC_PAGESIZE))
                    .put("physicalActiveFramesRequested",physicalActiveFrames).put("activeFramesRequested",activeFrames).put("fullDcevmComplete",false).put("privateEvolutionInitialized",false);
            phases.mark("native-load.before");
            System.loadLibrary("artdcevm");
            phases.mark("native-load.after");
            nativeLoaded=true;
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
            phases.mark("main-jni.before");
            int main=probe(carrier,Thread.currentThread());
            phases.mark("main-jni.after");
            report.put("mainCallbackHits",main);
            report.put("mainRoute",probeRoute());
            if(main!=128)throw new AssertionError("Owned JNI access main: "+main);
            phases.mark("main-context.before");
            int context=probeContext();report.put("mainContextChecks",context);
            phases.mark("main-context.after");
            if(context!=384)throw new AssertionError("Owned Context main: "+context);
            phases.mark("main-method-metadata.before");
            int model=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,1);
            phases.mark("main-method-metadata.after");
            report.put("mainMethodTableCount",methodTableCount());
            if(methodTableCount()!=methods.length)throw new AssertionError("Complete method table oracle");
            report.put("mainMethodModel",model).put("executableMirrorIdentityCount",methods.length).put("registeredNativeBindingCalibrated",true);
            if(model!=11&&model!=12)throw new AssertionError("Owned method identity main: "+model);
            if(runtimeServices||nativeCache||jitReceivers||methodGuards||activeFrames||physicalActiveFrames) {
                phases.mark("runtime-services.before");
                String serviceError=probeRuntimeServices();
                phases.mark("runtime-services.after");
                report.put("runtimeServicesError",serviceError).put("runtimeServicesCalibrated",serviceError!=null&&serviceError.isEmpty());
                if(serviceError==null||!serviceError.isEmpty())throw new AssertionError(serviceError);
            }
            if(runtimeServices||nativeCache||jitReceivers||methodGuards||activeFrames||physicalActiveFrames) {
                phases.mark("commit-method-metadata.before");
                int commitModel=probeMethods(methods,oracle.indices,oracle.modifiers,MethodIdentityFixture.class,
                        carrier,Thread.currentThread(),oracle.directMethods,oracle.virtualMethods,1);
                phases.mark("commit-method-metadata.after");
                report.put("mainCommitMethodModel",commitModel);
                if(commitModel!=model)throw new AssertionError("Owned modern method commit service: "+commitModel);
            }
            if(jitReceivers||activeFrames||physicalActiveFrames) {
                phases.mark("jit-services.before");
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
                phases.mark("jit-services.after");
            }
            if(activeFrames) {
                phases.mark("active-frames.before");
                // A missing frame proof must stay visible without discarding the
                // independent method/field results later in this same process.
                try { report.put("ownedActiveFrameProof",OwnedActiveFrameCalibration.run(apkPath)); }
                catch(Throwable error) {
                    report.put("ownedActiveFrameProof",new JSONObject().put("passed",false)
                        .put("error",android.util.Log.getStackTraceString(error))
                        .put("scope","owned compiled carrier/two-inlinee proof missing; no frame capability"));
                }
                phases.mark("active-frames.after");
            }
            if(physicalActiveFrames) {
                phases.mark("physical-active-frames.before");
                try { report.put("ownedPhysicalActiveFrameProof",OwnedActiveFrameCalibration.runPhysical(apkPath)); }
                catch(Throwable error) {
                    report.put("ownedPhysicalActiveFrameProof",new JSONObject().put("passed",false)
                        .put("error",android.util.Log.getStackTraceString(error))
                        .put("wholeProcessFrameEligibility",false));
                }
                phases.mark("physical-active-frames.after");
            }
            phases.mark("worker-jni.before");
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
            phases.mark("worker-jni.after");
            report.put("workerCallbackHits",new JSONArray(results));
            report.put("workerRoutes",new JSONArray(routes));
            report.put("workerContextChecks",new JSONArray(contexts));
            report.put("workerMethodModels",new JSONArray(models));
            report.put("workerMethodTableCounts",new JSONArray(tables));
            for(int i=0;i<2;i++) {
                if(workers[i].isAlive()||errors[i]!=null||results[i]!=128||contexts[i]!=384||models[i]!=model)
                    throw new AssertionError("Owned JNI access worker "+i+": "+results[i],errors[i]);
            }
            phases.mark("field-storage.before");
            report.put("fieldStorageModel",fields(fieldStorage));
            phases.mark("field-storage.after");
            if(fieldStorage)report.put("nativeFieldStorageCalibrated",true).put("nativeCardTableOffset",probeCardOffset());
            report.put("fieldIdentityCount",28);
            if(methodGuards) {
                phases.mark("member-guards.before");
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
                java.lang.invoke.MethodHandles.Lookup lookup=java.lang.invoke.MethodHandles.lookup();
                java.lang.invoke.MethodHandle getter=lookup.unreflectGetter(removedField),setter=lookup.unreflectSetter(removedField);
                java.lang.invoke.MethodHandle staticGetter=lookup.unreflectGetter(removedStatic),staticSetter=lookup.unreflectSetter(removedStatic);
                java.lang.invoke.MethodHandle keptGetter=lookup.unreflectGetter(RetiredEntryFixture.class.getDeclaredField("keptField"));
                String prepared=prepareFieldGuards(RetiredEntryFixture.class,fieldFixture,removedField,removedStatic,getter,staticGetter);
                if(prepared==null||!prepared.isEmpty())throw new AssertionError("Field handle preparation: "+prepared);
                // Both real original exported invocations must traverse the
                // calibrated same-owner targets before native retirement.
                proveFieldHandle(getter,removedField,() -> {if((int)getter.invokeExact(fieldFixture)!=41)throw new AssertionError("Owned instance field handle precondition");});
                proveFieldHandle(staticGetter,removedStatic,() -> {if((long)staticGetter.invokeExact()!=13L)throw new AssertionError("Owned static field handle precondition");});
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
                int handleRejected=0;
                try {int value=(int)getter.invokeExact(fieldFixture);}catch(NoSuchFieldError expected){handleRejected++;}
                try {setter.invokeExact(fieldFixture,99);}catch(NoSuchFieldError expected){handleRejected++;}
                try {long value=(long)staticGetter.invokeExact();}catch(NoSuchFieldError expected){handleRejected++;}
                try {staticSetter.invokeExact(99L);}catch(NoSuchFieldError expected){handleRejected++;}
                try {int value=(int)getter.invoke((Object)fieldFixture);}catch(NoSuchFieldError expected){handleRejected++;}
                try {setter.invoke((Object)fieldFixture,(Object)Integer.valueOf(99));}catch(NoSuchFieldError expected){handleRejected++;}
                try {long value=(long)staticGetter.invoke();}catch(NoSuchFieldError expected){handleRejected++;}
                try {staticSetter.invoke((Object)Long.valueOf(99));}catch(NoSuchFieldError expected){handleRejected++;}
                try {getter.invokeWithArguments(fieldFixture);}catch(NoSuchFieldError expected){handleRejected++;}
                try {setter.invokeWithArguments(fieldFixture,99);}catch(NoSuchFieldError expected){handleRejected++;}
                try {staticGetter.invokeWithArguments();}catch(NoSuchFieldError expected){handleRejected++;}
                try {staticSetter.invokeWithArguments(99L);}catch(NoSuchFieldError expected){handleRejected++;}
                if(handleRejected!=12||(int)keptGetter.invokeExact(fieldFixture)!=73)throw new AssertionError("Retired native MethodHandle field access accepted");
                report.put("methodHandleFieldGuardsCalibrated",true).put("methodHandleFieldGuardChecks",handleRejected);
                int[] beforeCleanup=cleanupCounts();
                java.lang.ref.WeakReference<ClassLoader> loader=allocatorFixture(apkPath);
                int[] cleanup=null;
                for(int attempt=0;attempt<100;attempt++) {
                    System.gc();System.runFinalization();Thread.sleep(20);cleanup=cleanupCounts();
                    if(loader.get()==null&&cleanup[1]>beforeCleanup[1]&&cleanup[2]>beforeCleanup[2])break;
                }
                if(loader.get()!=null||cleanup[1]<=beforeCleanup[1]||cleanup[2]<=beforeCleanup[2]||cleanup[3]!=beforeCleanup[3]||cleanup[4]!=beforeCleanup[4])
                    throw new AssertionError("Native loader pruning did not restore registry counts: "+java.util.Arrays.toString(cleanup));
                report.put("retirementCleanupCounts",new JSONArray(cleanup)).put("retirementCleanupCalibrated",true);
                phases.mark("member-guards.after");
            }
            if(exclusiveRoots) {
                phases.mark("exclusive-roots.before");
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
                phases.mark("exclusive-roots.after");
            }
            if(nativeCache) {
                phases.mark("native-cache.before");
                String cacheError=probeNativeCache();int cacheThreads=probeCacheThreads();
                phases.mark("native-cache.after");
                report.put("nativeCacheError",cacheError).put("nativeCacheClearedThreads",cacheThreads);
                if(cacheError==null||!cacheError.isEmpty()||cacheThreads<=0)
                    throw new AssertionError("Native cache clearing: "+cacheError);
            }
            // Runtime/JIT receivers are prerequisites for a physical frame query.
            // Mutation remains independently requested by its guard/storage scope.
            if(runtimeServices&&jitReceivers&&(!physicalActiveFrames||methodGuards||fieldStorage)) {
                com.github.ytlog.artdcevm.fixture.Evolving instance=new com.github.ytlog.artdcevm.fixture.Evolving(31);
                Class<?> owner=instance.getClass();Method cached=owner.getDeclaredMethod("addedMethod");
                int identity=System.identityHashCode(instance);
                MethodHandles.Lookup lookup=MethodHandles.lookup();
                MethodHandle exact=lookup.findVirtual(owner,"addedMethod",MethodType.methodType(int.class));
                MethodHandle surviving=lookup.findVirtual(owner,"compute",MethodType.methodType(int.class));
                MethodHandle statik=lookup.findStatic(RetiredEntryFixture.class,"kept",MethodType.methodType(int.class));
                MethodHandle adapted=exact.asType(MethodType.methodType(Object.class,Object.class));
                int baseline=(Integer)cached.invoke(instance);
                for(int warm=0;warm<5000;++warm) {
                    if((int)surviving.invokeExact(instance)!=34||(int)exact.invokeExact(instance)!=baseline)throw new AssertionError("Cached MethodHandle warmup");
                }
                byte[] deletion;
                try(java.util.zip.ZipFile zip=new java.util.zip.ZipFile(apkPath);
                    java.io.InputStream in=zip.getInputStream(zip.getEntry("assets/method-delete/com/github/ytlog/artdcevm/fixture/Evolving.dex"))) {
                    java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[8192];int n;
                    while((n=in.read(chunk))!=-1)bytes.write(chunk,0,n);deletion=bytes.toByteArray();
                }
                phases.mark("method-deletion.before");
                String error=probeOwnedMethodDeletion(owner,instance,deletion,new MethodHandle[]{exact,surviving,statik},baseline);
                phases.mark("method-deletion.after");
                report.put("ownedMethodDeletionError",error);
                if(error==null||!error.isEmpty())throw new AssertionError("Owned method deletion: "+error);
                long diagnostics=probeOwnedCommitDiagnostics();
                if((diagnostics&0xffffffffL)==0||(diagnostics&(1L<<32))==0)
                    throw new AssertionError("Actual native cache/declaring root readback missing");
                int handleRejected=0;
                try {int ignored=(int)exact.invokeExact(instance);}catch(NoSuchMethodError expected){handleRejected++;}
                try {Object ignored=(Object)adapted.invokeExact((Object)instance);}catch(NoSuchMethodError expected){handleRejected++;}
                if(handleRejected!=2||(int)surviving.invokeExact(instance)!=35)
                    throw new AssertionError("Cached MethodHandle deletion/retained target");
                collectAndVerifyGc(report,"ownedMethodDeletion");
                try {int ignored=(int)exact.invokeExact(instance);throw new AssertionError("Retired MethodHandle accepted after GC");}
                catch(NoSuchMethodError expected){}
                boolean absent=false,rejected=false;
                try {instance.getClass().getDeclaredMethod("addedMethod");}catch(NoSuchMethodException expected){absent=true;}
                try {cached.invoke(instance);}catch(java.lang.reflect.InvocationTargetException wrapped){
                    if(wrapped.getCause() instanceof NoSuchMethodError)rejected=true;else throw wrapped;
                }catch(NoSuchMethodError expected){rejected=true;}
                if(!absent||!rejected||instance.compute()!=35||instance.value!=31||System.identityHashCode(instance)!=identity)
                    throw new AssertionError("Owned method deletion changed retained values, identity or retired reflection behavior");
                report.put("ownedMethodHandleTargetProof",true)
                    .put("ownedMethodDeletionMethodHandleRejected",true).put("ownedMethodDeletionMethodHandleChecks",2)
                    .put("ownedMethodDeletionKeptMethodHandleValue",35).put("ownedMethodDeletionIdentityPreserved",true)
                    .put("ownedMethodDeletionCacheClearedThreads",diagnostics&0xffffffffL)
                    .put("ownedMethodDeletionDeclaringRootsRestored",true).put("ownedMethodDeletionAfterGcChecks",1)
                    .put("ownedMethodDeletion",true).put("ownedMethodDeletionJniRejected",true)
                    .put("ownedMethodDeletionReflectionRejected",true).put("ownedMethodDeletionKeptValue",instance.compute());
            }
            if(runtimeServices&&jitReceivers&&methodGuards&&fieldStorage){
                phases.mark("field-deletion.before");ownedFieldDeletion(apkPath,report,phases);phases.mark("field-deletion.after");
            }
            report.put("passed",true);
        } catch(Throwable error) {
            try {report.put("passed",false).put("error",android.util.Log.getStackTraceString(error));}
            catch(Exception ignored) { }
        } finally {
            if(nativeLoaded) {
                try {phases.mark("field-stage-release.before");releaseFieldProofStages();phases.mark("field-stage-release.after");}
                catch(Throwable error) {
                    try {report.put("passed",false).put("fieldProofCleanupError",android.util.Log.getStackTraceString(error));}
                    catch(Exception ignored) { }
                }
            }
        }
        return report;
    }
}
