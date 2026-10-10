package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.content.Context;
import android.os.Bundle;
import android.util.AtomicFile;
import android.util.Log;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.FinalFieldSubject;
import com.github.ytlog.artdcevm.fixture.PrivateFieldSubject;
import dalvik.system.InMemoryDexClassLoader;
import java.io.*;
import java.lang.invoke.*;
import java.lang.reflect.*;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;
import org.json.JSONArray;

/** Real production applies; cached public JNI entry points and Java access authorities. */
public final class ProductionPermissionScenarios {
    private final Instrumentation instrumentation;
    private final Bundle arguments;
    private Context context;
    private JSONObject report;
    private final JSONArray applies=new JSONArray();
    private TestPhaseLog phases;
    private boolean supportLoaded;
    interface Action {void run() throws Throwable;}
    public ProductionPermissionScenarios(Instrumentation instrumentation,Bundle arguments) {
        this.instrumentation=instrumentation;this.arguments=arguments==null?new Bundle():new Bundle(arguments);
    }
    private static void check(boolean value,String message) {if(!value)throw new AssertionError(message);}
    private static void equal(int expected,int actual) {check(expected==actual,"Expected "+expected+", got "+actual);}
    private void denied(Class<? extends Throwable> type,Action action) throws Throwable {
        Throwable caught=null;
        try {action.run();}catch(InvocationTargetException e){caught=e.getCause();}catch(Throwable e){caught=e;}
        check(type.isInstance(caught),"Expected "+type.getName()+", got "+caught);
    }
    private byte[] asset(String path) throws IOException {
        try(InputStream in=context.getAssets().open(path);ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            byte[] bytes=new byte[8192];int n;while((n=in.read(bytes))!=-1)out.write(bytes,0,n);return out.toByteArray();
        }
    }
    private void captureFailure(Throwable original) {
        ProductionFailureCapture.record(report, original, new ProductionFailureCapture.Probe() {
            public String capabilities() { return NativeBridge.capabilities(); }
            public String errorName(int code) { return NativeBridge.errorName(code); }
        });
    }
    private void stage(String value) throws Exception {report.put("stage",value);phases.mark("permission."+value);}
    private void apply(String variant,Class<?> cls) throws Exception {
        stage("apply-"+variant+".before");
        byte[] replacement=asset(variant+"/"+cls.getName().replace('.','/')+".dex");
        stage("apply-"+variant+".native.before");
        int result=NativeBridge.apply(2,new Class<?>[]{cls},new byte[][]{replacement});
        applies.put(new JSONObject().put("variant",variant).put("code",result));
        report.put("capabilitiesAfterApply",new JSONObject(NativeBridge.capabilities()));
        equal(0,result);stage("apply-"+variant+".after");
    }
    private void gc(Object object,Class<?> cls,int identity) {
        System.gc();byte[][] pressure=new byte[16][];for(int i=0;i<pressure.length;i++)pressure[i]=new byte[65536];System.gc();
        check(object.getClass()==cls,"Old object class changed");equal(identity,System.identityHashCode(object));
    }
    public void run(String name) throws Throwable {
        check("final-fields".equals(name)||"field-access".equals(name),"Unknown permission case");
        context=instrumentation.getTargetContext();String runId=arguments.getString("productionRunId");
        if(runId==null)runId=java.util.UUID.randomUUID().toString().replace("-","");
        check(runId.matches("[0-9a-f]{32}"),"Malformed run ID");
        String scope=arguments.getString("productionPermissionScope","full");
        check(scope.equals("full")||scope.equals("common-owned"),"Unknown initialization scope");
        report=new JSONObject().put("runId",runId).put("case",name).put("initializationScope",scope)
            .put("passed",false).put("sourceEngineUsed",false).put("route","DcevmRuntime/NativeBridge.apply").put("applies",applies);
        phases=new TestPhaseLog(runId,context.getFilesDir());
        TestForegroundScope foreground=new TestForegroundScope(instrumentation,arguments,report,phases);
        try {
            foreground.start();
            stage("initialize.before");report.put("initialCapabilities",scope.equals("common-owned")
                ?ProductionCommonOwnerInitializer.initialize(context):DcevmRuntime.initializeCapabilities(context));
            if(scope.equals("full")){stage("required-full-initialize.before");DcevmRuntime.initialize(context);}
            JSONObject caps=new JSONObject(NativeBridge.capabilities());report.put("capabilities",caps);
            check(scope.equals("full")?caps.optBoolean("evolutionAvailable"):
                caps.optString("backend").equals("runtime-capabilities")&&caps.optBoolean("partialEvolutionAvailable"),"Production owner unavailable: "+caps);
            stage("support-load.before");System.loadLibrary("artdcevm_test_support");supportLoaded=true;stage("scenario.before");
            if(name.equals("final-fields"))finalFields();else fieldAccess();
            stage("finished");report.put("passed",true);
        } catch(Throwable e) {captureFailure(e);throw e;
        } finally {
            boolean completed=report.optBoolean("passed");
            if(supportLoaded)ProductionPermissionNative.release();
            boolean foregroundValid=foreground.finish();
            if(!foregroundValid){report.put("passed",false);report.put("foregroundFailure","Foreground carrier lost its same-process resumed lifetime");}
            File dir=new File(context.getFilesDir(),"production-permissions");
            try {
                if(!dir.isDirectory()&&!dir.mkdirs())throw new IOException("Cannot create report directory");
                AtomicFile file=new AtomicFile(new File(dir,runId+".json"));FileOutputStream out=null;
                try {out=file.startWrite();out.write(report.toString().getBytes(StandardCharsets.UTF_8));file.finishWrite(out);}
                catch(IOException e){file.failWrite(out);throw e;}
            } catch(IOException e){report.put("reportPersistenceError",e.toString());}
            Bundle output=new Bundle();output.putString("productionPermissionReport",report.toString());instrumentation.addResults(output);
            Log.i("ArtDcevmProductionPermission",report.toString());
            if(completed&&!foregroundValid)throw new AssertionError("Foreground carrier proof failed");
        }
    }
    private void finalFields() throws Throwable {
        FinalFieldSubject object=new FinalFieldSubject();Class<?> cls=object.getClass();int identity=System.identityHashCode(object);
        Field field=cls.getField("value"),shared=cls.getField("staticValue");
        MethodHandles.Lookup lookup=MethodHandles.lookup();
        MethodHandle setter=lookup.findSetter(cls,"value",int.class),staticSetter=lookup.findStaticSetter(cls,"staticValue",int.class);
        MethodHandle getter=lookup.findGetter(cls,"value",int.class),staticGetter=lookup.findStaticGetter(cls,"staticValue",int.class);
        VarHandle handle=lookup.findVarHandle(cls,"value",int.class),staticHandle=lookup.findStaticVarHandle(cls,"staticValue",int.class);
        ProductionPermissionNative.retain(cls,object,"value","staticValue");
        for(int i=0;i<256;i++){FinalFieldCaller.write(object,73);FinalFieldCaller.writeStatic(84);setter.invoke(object,73);staticSetter.invoke(84);}
        apply("modifier-field-final",cls);
        check(Modifier.isFinal(field.getModifiers())&&Modifier.isFinal(shared.getModifiers()),"Cached reflection final flags stale");
        check(Modifier.isFinal(cls.getField("value").getModifiers())&&Modifier.isFinal(cls.getField("staticValue").getModifiers()),"Fresh final flags missing");
        equal(73,field.getInt(object));equal(84,shared.getInt(null));equal(73,(int)getter.invoke(object));equal(84,(int)staticGetter.invoke());
        equal(73,(int)handle.get(object));equal(84,(int)staticHandle.get());equal(73,FinalFieldCaller.read(object));equal(84,FinalFieldCaller.readStatic());
        denied(IllegalAccessException.class,()->field.setInt(object,1));denied(IllegalAccessException.class,()->shared.setInt(null,1));
        denied(IllegalAccessError.class,()->setter.invoke(object,1));denied(IllegalAccessError.class,()->staticSetter.invoke(1));
        denied(UnsupportedOperationException.class,()->handle.set(object,1));denied(UnsupportedOperationException.class,()->staticHandle.set(1));
        denied(UnsupportedOperationException.class,()->handle.compareAndSet(object,73,1));denied(UnsupportedOperationException.class,()->staticHandle.getAndAdd(1));
        denied(IllegalAccessError.class,()->FinalFieldCaller.write(object,1));denied(IllegalAccessError.class,()->FinalFieldCaller.writeStatic(1));
        denied(IllegalAccessException.class,()->lookup.findSetter(cls,"value",int.class));denied(IllegalAccessException.class,()->lookup.findStaticSetter(cls,"staticValue",int.class));
        Field fresh=cls.getField("value"),freshStatic=cls.getField("staticValue");
        denied(IllegalAccessException.class,()->fresh.setInt(object,1));denied(IllegalAccessException.class,()->freshStatic.setInt(null,1));
        VarHandle freshHandle=lookup.findVarHandle(cls,"value",int.class),freshStaticHandle=lookup.findStaticVarHandle(cls,"staticValue",int.class);
        equal(73,(int)freshHandle.get(object));equal(84,(int)freshStaticHandle.get());
        denied(UnsupportedOperationException.class,()->freshHandle.set(object,1));denied(UnsupportedOperationException.class,()->freshStaticHandle.set(1));
        report.put("finalDeniedCachedAndFresh",true).put("foreignFinalWritesDenied",true);
        // ART public JNI is an authorized native write path even for final fields.
        ProductionPermissionNative.write(false,91);ProductionPermissionNative.write(true,92);
        equal(91,ProductionPermissionNative.read(false));equal(92,ProductionPermissionNative.read(true));
        equal(91,field.getInt(object));equal(92,shared.getInt(null));
        gc(object,cls,identity);equal(91,(int)handle.get(object));equal(92,(int)staticHandle.get());
        report.put("cachedJniFinalWritesLegal",true).put("oldObjectGcState",true);
        apply("modifier-field-original",cls);
        check(!Modifier.isFinal(field.getModifiers())&&!Modifier.isFinal(shared.getModifiers()),"Cached mutable flags stale");
        field.setInt(object,101);shared.setInt(null,102);setter.invoke(object,103);staticSetter.invoke(104);handle.set(object,105);staticHandle.set(106);
        FinalFieldCaller.write(object,107);FinalFieldCaller.writeStatic(108);
        equal(107,ProductionPermissionNative.read(false));equal(108,ProductionPermissionNative.read(true));
        equal(107,cls.getField("value").getInt(object));equal(108,cls.getField("staticValue").getInt(null));
        report.put("mutableRestoredAllFourPaths",true);
    }
    private Class<?> foreign() throws Exception {
        String name="com.github.ytlog.artdcevm.external.PrivateFieldCaller";
        return new InMemoryDexClassLoader(ByteBuffer.wrap(asset("private-field-external/"+name.replace('.','/')+".dex")),PrivateFieldSubject.class.getClassLoader()).loadClass(name);
    }
    private void fieldAccess() throws Throwable {
        PrivateFieldSubject object=new PrivateFieldSubject();Class<?> cls=object.getClass();int identity=System.identityHashCode(object);
        Field privileged=cls.getDeclaredField("value"),privilegedStatic=cls.getDeclaredField("shared");privileged.setAccessible(true);privilegedStatic.setAccessible(true);
        ProductionPermissionNative.retain(cls,object,"value","shared");
        Method before=foreign().getMethod("read",cls);denied(IllegalAccessError.class,()->before.invoke(null,object));
        apply("private-field-public",cls);
        Field ordinary=cls.getField("value"),ordinaryStatic=cls.getField("shared");
        MethodHandles.Lookup lookup=MethodHandles.publicLookup();
        MethodHandle setter=lookup.findSetter(cls,"value",int.class),staticSetter=lookup.findStaticSetter(cls,"shared",int.class);
        VarHandle handle=lookup.findVarHandle(cls,"value",int.class),staticHandle=lookup.findStaticVarHandle(cls,"shared",int.class);
        Method read=foreign().getMethod("read",cls),write=read.getDeclaringClass().getMethod("write",cls,int.class,int.class);
        ordinary.setInt(object,31);ordinaryStatic.setInt(null,32);equal(63,(int)read.invoke(null,object));write.invoke(null,object,41,42);
        equal(41,ProductionPermissionNative.read(false));equal(42,ProductionPermissionNative.read(true));
        report.put("foreignPublicReadWriteAllowed",true);
        apply("private-field-private",cls);
        check(Modifier.isPrivate(ordinary.getModifiers())&&Modifier.isPrivate(ordinaryStatic.getModifiers()),"Cached private flags stale");
        check(Modifier.isPrivate(cls.getDeclaredField("value").getModifiers())&&Modifier.isPrivate(cls.getDeclaredField("shared").getModifiers()),"Fresh private flags missing");
        denied(IllegalAccessException.class,()->ordinary.getInt(object));denied(IllegalAccessException.class,()->ordinaryStatic.getInt(null));
        denied(IllegalAccessException.class,()->ordinary.setInt(object,1));denied(IllegalAccessException.class,()->ordinaryStatic.setInt(null,1));
        denied(IllegalAccessException.class,()->cls.getDeclaredField("value").getInt(object));
        denied(IllegalAccessException.class,()->lookup.findSetter(cls,"value",int.class));denied(IllegalAccessException.class,()->lookup.findStaticVarHandle(cls,"shared",int.class));
        denied(IllegalAccessError.class,()->read.invoke(null,object));denied(IllegalAccessError.class,()->write.invoke(null,object,1,1));
        Method freshRead=foreign().getMethod("read",cls);denied(IllegalAccessError.class,()->freshRead.invoke(null,object));
        report.put("foreignPrivateCachedAndFreshDenied",true).put("ordinaryReflectionAndFreshLookupDenied",true);
        // MH/VH obtained with public access keep their creation-time authority.
        setter.invoke(object,51);staticSetter.invoke(52);equal(51,(int)handle.get(object));equal(52,(int)staticHandle.get());
        handle.set(object,61);staticHandle.set(62);privileged.setInt(object,71);privilegedStatic.setInt(null,72);
        ProductionPermissionNative.write(false,81);ProductionPermissionNative.write(true,82);
        equal(81,object.read());equal(82,PrivateFieldSubject.readShared());
        gc(object,cls,identity);equal(81,privileged.getInt(object));equal(82,privilegedStatic.getInt(null));
        equal(81,ProductionPermissionNative.read(false));equal(82,ProductionPermissionNative.read(true));
        report.put("cachedAuthorizedFourPathsRetained",true).put("oldObjectGcState",true);
        apply("private-field-public",cls);write.invoke(null,object,91,92);equal(183,(int)read.invoke(null,object));
        equal(91,ordinary.getInt(object));equal(92,ordinaryStatic.getInt(null));report.put("publicRestored",true);
    }
}
