package com.github.ytlog.artdcevm.testing;
import android.content.Context;
import android.app.Instrumentation;
import android.util.Log;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import androidx.test.platform.app.InstrumentationRegistry;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.Evolving;
import com.github.ytlog.artdcevm.fixture.FieldSubject;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.*;
public final class ProductionCapabilitiesTest {
    private byte[] asset(Context context,String variant) throws Exception { return asset(context,variant,"Evolving"); }
    private byte[] asset(Context context,String variant,String name) throws Exception {
        try (InputStream in=context.getAssets().open(variant+"/com/github/ytlog/artdcevm/fixture/"+name+".dex");ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1)out.write(b,0,n);return out.toByteArray();
        }
    }
    interface Checked { void run() throws Throwable; }
    private void fieldRejected(Checked action) throws Throwable {
        try { action.run();fail("Retired field handle accepted"); }catch(NoSuchFieldError expected) {}
    }
    private void applyRequired(JSONObject report,String stage,int operation,Class<?> type,byte[] dex) throws Exception {
        report.put("lastApplyStage",stage);
        int code=NativeBridge.apply(operation,new Class<?>[]{type},new byte[][]{dex});
        report.put("lastApplyCode",code);
        if(code!=0){
            JSONObject diagnostics=new JSONObject(NativeBridge.capabilities());
            report.put("capabilitiesAfterFailure",diagnostics);
            assertEquals(stage+": "+diagnostics,0,code);
        }
    }
    @Test public void productionBootstrapAndFormalEvolution() throws Throwable {
        String runId=InstrumentationRegistry.getArguments().getString("productionRunId");
        if(runId==null)runId=java.util.UUID.randomUUID().toString().replace("-","");
        assertTrue("Malformed production run correlation",runId.matches("[0-9a-f]{32}"));
        Instrumentation instrumentation=InstrumentationRegistry.getInstrumentation();
        Context context=instrumentation.getTargetContext();
        TestPhaseLog phases=new TestPhaseLog(runId,context.getFilesDir());phases.mark("production.begin");
        JSONObject report=new JSONObject();boolean completed=false;
        report.put("runId",runId);
        TestForegroundScope foreground=new TestForegroundScope(instrumentation,
                InstrumentationRegistry.getArguments(),report,phases);
        try {
        foreground.start();
        phases.mark("capabilities.before");
        JSONObject capabilities=DcevmRuntime.initializeCapabilities(context);
        phases.mark("capabilities.after");
        phases.mark("support-load.before");System.loadLibrary("artdcevm_test_support");phases.mark("support-load.after");
        assertTrue(capabilities.toString(),capabilities.optBoolean("nativeMethodDeletion"));
        phases.mark("additive.before");
        applyRequired(report,"additive",1,Evolving.class,asset(context,"add"));
        phases.mark("additive.after");
        Evolving instance=new Evolving(31);Method cached=instance.getClass().getDeclaredMethod("addedMethod");
        int identity=System.identityHashCode(instance);NativeTestBridge.retainRemovalMethod(instance);
        phases.mark("method-deletion.before");
        applyRequired(report,"method-deletion",2,Evolving.class,asset(context,"method-delete"));
        phases.mark("method-deletion.after");
        assertEquals(35,instance.compute());assertEquals(31,instance.value);assertEquals(identity,System.identityHashCode(instance));
        try {cached.invoke(instance);fail("Retired reflected method accepted");}
        catch (InvocationTargetException wrapped) {assertTrue(wrapped.getCause() instanceof NoSuchMethodError);}
        catch (NoSuchMethodError expected) {}
        for(int mode=0;mode<4;mode++)try {NativeTestBridge.invokeRemovalMethod(mode);fail("Retired JNI accepted mode "+mode);}catch(NoSuchMethodError expected){}
        assertTrue(capabilities.toString(),capabilities.optBoolean("fieldDeletion"));
        FieldSubject fields=new FieldSubject();int fieldIdentity=System.identityHashCode(fields);
        NativeTestBridge.retainFields(fields);
        java.util.ArrayList<java.lang.reflect.Field> cachedFields=new java.util.ArrayList<>();
        java.util.ArrayList<Object> originalFields=new java.util.ArrayList<>();
        for(java.lang.reflect.Field field:FieldSubject.class.getDeclaredFields()){
            if(field.getName().equals("keep")||field.getName().equals("staticKeep"))continue;
            cachedFields.add(field);originalFields.add(field.get(fields));
        }
        assertEquals(18,cachedFields.size());
        MethodHandles.Lookup lookup=MethodHandles.lookup();
        MethodHandle getter=lookup.findGetter(FieldSubject.class,"i",int.class);
        MethodHandle setter=lookup.findSetter(FieldSubject.class,"i",int.class);
        MethodHandle staticGetter=lookup.findStaticGetter(FieldSubject.class,"si",int.class);
        MethodHandle staticSetter=lookup.findStaticSetter(FieldSubject.class,"si",int.class);
        VarHandle vh=lookup.findVarHandle(FieldSubject.class,"i",int.class);
        VarHandle svh=lookup.findStaticVarHandle(FieldSubject.class,"si",int.class);
        assertEquals(7,(int)getter.invoke(fields));assertEquals(7,(int)staticGetter.invoke());
        assertEquals(7,(int)vh.get(fields));assertEquals(7,(int)svh.get());
        phases.mark("field-deletion.before");
        applyRequired(report,"field-deletion",2,FieldSubject.class,asset(context,"field-delete","FieldSubject"));
        phases.mark("field-deletion.after");
        assertEquals(41,fields.keep);assertEquals(42,FieldSubject.staticKeep);assertEquals(42,fields.compute());
        assertEquals(fieldIdentity,System.identityHashCode(fields));
        assertNull(com.github.ytlog.artdcevm.internal.ProductionBootstrap.verifyLiveFieldId(FieldSubject.class.getDeclaredField("keep")));
        assertNull(com.github.ytlog.artdcevm.internal.ProductionBootstrap.verifyLiveFieldId(FieldSubject.class.getDeclaredField("staticKeep")));
        assertEquals("Fresh retained JNI field IDs",4,NativeTestBridge.proveFreshKeptFieldIds(fields));
        assertEquals(41,fields.keep);assertEquals(42,FieldSubject.staticKeep);
        for(int index=0;index<18;index++)for(boolean write:new boolean[]{false,true}){
            final int slot=index;fieldRejected(() -> NativeTestBridge.accessField(slot,write));
        }
        for(int index=0;index<cachedFields.size();index++){
            java.lang.reflect.Field field=cachedFields.get(index);Object value=originalFields.get(index);
            fieldRejected(() -> field.get(fields));fieldRejected(() -> field.set(fields,value));
        }
        fieldRejected(() -> getter.invoke(fields));fieldRejected(() -> setter.invoke(fields,9));
        fieldRejected(() -> staticGetter.invoke());fieldRejected(() -> staticSetter.invoke(9));
        fieldRejected(() -> vh.get(fields));fieldRejected(() -> vh.set(fields,9));
        fieldRejected(() -> svh.get());fieldRejected(() -> svh.set(9));
        report.put("formalApplyFieldDeletion",true).put("fieldFreshKeptJniChecks",4).put("fieldHandleRejectedPaths",8).put("fieldJniRejectedPaths",36).put("fieldReflectionRejectedPaths",36).put("fieldRetainedValue",fields.keep).put("fieldStaticRetainedValue",FieldSubject.staticKeep).put("fieldRetainedCompute",fields.compute()).put("fieldRetainedIdentity",System.identityHashCode(fields)==fieldIdentity);
report.put("runId",runId).put("cachedReflectionRejected",true).put("retainedIdentity",System.identityHashCode(instance)==identity).put("productionBootstrap",true).put("formalApplyMethodDeletion",true).put("cachedJniRejectedModes",4).put("retainedCompute",instance.compute()).put("retainedValue",instance.value).put("capabilities",new JSONObject(NativeBridge.capabilities()));
        completed=true;
        } finally {
            boolean validForeground=foreground.finish();
            phases.mark("production.finish");
            File directory=new File(context.getFilesDir(),"production-bootstrap");
            if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("Cannot create production report directory");
            AtomicFile file=new AtomicFile(new File(directory,runId+".json"));
            FileOutputStream stream=null;
            try {
                stream=file.startWrite();stream.write(report.toString().getBytes(StandardCharsets.UTF_8));file.finishWrite(stream);
            } catch(IOException error) {file.failWrite(stream);throw error;}
            Log.i("ArtDcevmProduction",report.toString());
            if(completed)assertTrue("Foreground carrier lost foreground or failed to finish",validForeground);
        }
    }
}
