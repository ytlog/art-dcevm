package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Bundle;
import android.util.Log;
import android.util.AtomicFile;
import android.app.Instrumentation;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.*;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.lang.invoke.*;
import java.lang.reflect.*;
import org.json.JSONObject;
import java.util.Objects;

/** Formal public initialization and shared production apply only; no SourceEngine route. */
public final class ProductionSchemaScenarios {
    private final Instrumentation instrumentation;
    private final Bundle arguments;
    public ProductionSchemaScenarios(Instrumentation instrumentation,Bundle arguments) {
        this.instrumentation=Objects.requireNonNull(instrumentation);
        this.arguments=arguments==null?new Bundle():new Bundle(arguments);
    }
    private static void assertTrue(String message,boolean value) {if(!value)throw new AssertionError(message);}
    private static void assertTrue(boolean value) {assertTrue("Expected true",value);}
    private static void assertFalse(boolean value) {assertTrue("Expected false",!value);}
    private static void assertEquals(String message,long expected,long actual) {if(expected!=actual)throw new AssertionError(message+": expected "+expected+", got "+actual);}
    private static void assertEquals(long expected,long actual) {assertEquals("Values differ",expected,actual);}
    private static void assertEquals(String message,Object expected,Object actual) {if(!Objects.equals(expected,actual))throw new AssertionError(message+": expected "+expected+", got "+actual);}
    private static void assertEquals(Object expected,Object actual) {assertEquals("Values differ",expected,actual);}
    private static void assertSame(Object expected,Object actual) {assertTrue("References differ",expected==actual);}
    private static void assertNull(Object actual) {assertSame(null,actual);}
    public void run(String name) throws Throwable {
        switch(name) {
            case "field-types":fieldTypeEvolutionRequired();break;
            case "field-staticness":fieldStorageEvolutionRequired();break;
            case "method-signature":methodSignatureEvolutionRequired();break;
            case "native-java":nativeJavaTransitionsRequired();break;
            case "hierarchy-addition":hierarchyAdditionRequired();break;
            case "hierarchy-narrowing":hierarchyNarrowingRequired();break;
            default:throw new IllegalArgumentException("Unknown schema case: "+name);
        }
    }
    interface Action { void run() throws Throwable; }
    private Context context;
    private JSONObject report;
    private TestPhaseLog phases;
    private void persistReport(String runId) throws IOException {
        File directory=new File(context.getFilesDir(),"production-schema");
        if(!directory.isDirectory()&&!directory.mkdirs())throw new IOException("Cannot create schema report directory");
        AtomicFile file=new AtomicFile(new File(directory,runId+".json"));
        FileOutputStream stream=null;
        try {
            stream=file.startWrite();stream.write(report.toString().getBytes(StandardCharsets.UTF_8));
            file.finishWrite(stream);
        } catch(IOException error) {file.failWrite(stream);throw error;}
    }
    private void stage(String name) throws Exception { report.put("stage",name);phases.mark("schema."+name); }
    private byte[] asset(String variant,Class<?> cls) throws Exception {
        String path=variant+"/"+cls.getName().replace('.','/')+".dex";
        try(InputStream in=context.getAssets().open(path);ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            byte[] bytes=new byte[8192];int count;while((count=in.read(bytes))!=-1)out.write(bytes,0,count);return out.toByteArray();
        }
    }
    private void apply(String variant,Class<?> cls) throws Exception {
        stage("apply-"+variant+".before");
        int result=NativeBridge.apply(2,new Class<?>[]{cls},new byte[][]{asset(variant,cls)});
        report.put("lastApplyCode",result);
        report.put("capabilitiesAfterApply",new JSONObject(NativeBridge.capabilities()));
        assertEquals(variant+": "+NativeBridge.errorName(result),0,result);
        report.put("successfulApplyCount",report.optInt("successfulApplyCount")+1);
        stage("apply-"+variant+".after");
    }
    private void rejected(Class<? extends Throwable> type,Action action) throws Throwable {
        Throwable caught=null;
        try {action.run();}catch(InvocationTargetException error){caught=error.getCause();}catch(Throwable error){caught=error;}
        assertTrue("Expected "+type.getName()+", got "+caught,type.isInstance(caught));
    }
    private void required(String name,Action action) throws Throwable {
        context=instrumentation.getTargetContext();
        String runId=arguments.getString("productionRunId");
        if(runId==null)runId=java.util.UUID.randomUUID().toString().replace("-","");
        assertTrue("Malformed run ID",runId.matches("[0-9a-f]{32}"));
        String scope=arguments.getString("productionSchemaScope", "full");
        assertTrue("Unknown schema initialization scope", scope.equals("full") || scope.equals("common-owned"));
        report=new JSONObject().put("initializationScope",scope).put("runId",runId).put("case",name).put("passed",false)
            .put("route","DcevmRuntime/NativeBridge.apply").put("sourceEngineUsed",false);
        phases=new TestPhaseLog(runId,context.getFilesDir());
        try {
            stage("capabilities.before");report.put("initialCapabilities",DcevmRuntime.initializeCapabilities(context));
            if (scope.equals("full")) {
                stage("required-full-initialize.before");DcevmRuntime.initialize(context);
            } else stage("required-common-owned.before");
            JSONObject capabilities=new JSONObject(NativeBridge.capabilities());report.put("capabilities",capabilities);
            if (scope.equals("full")) {
                assertTrue("Required production schema capability unavailable: "+capabilities,capabilities.optBoolean("evolutionAvailable"));
            } else {
                assertEquals("Required shared owner route", "runtime-capabilities", capabilities.optString("backend"));
                assertTrue("Shared owner was not initialized: "+capabilities,capabilities.optBoolean("partialEvolutionAvailable"));
            }
            stage("support-load.before");System.loadLibrary("artdcevm_test_support");
            stage("scenario.before");action.run();stage("finished");report.put("passed",true);
        } catch(Throwable error) {
            report.put("failureType",error.getClass().getName()).put("failure",String.valueOf(error.getMessage()));throw error;
        } finally {
            try {persistReport(runId);} catch(IOException error) {
                report.put("reportPersistenceError",String.valueOf(error));
                Log.e("ArtDcevmProductionSchema","Cannot persist current-run report",error);
            }
            Bundle output=new Bundle();output.putString("productionSchemaReport",report.toString());
            instrumentation.addResults(output);
            Log.i("ArtDcevmProductionSchema",report.toString());
        }
    }
    private void fieldTypeEvolutionRequired() throws Throwable { required("field-types",()->{
        FieldSubject object=new FieldSubject();object.keep=101;FieldSubject.staticKeep=102;
        int identity=System.identityHashCode(object);Class<?> cls=object.getClass();
        Field old=cls.getField("i"),oldStatic=cls.getField("si");NativeTestBridge.retainFields(object);
        VarHandle oldHandle=MethodHandles.lookup().findVarHandle(cls,"i",int.class);
        apply("field-types",cls);
        rejected(NoSuchFieldError.class,()->old.getInt(object));rejected(NoSuchFieldError.class,()->oldStatic.getInt(null));
        rejected(NoSuchFieldError.class,()->oldHandle.get(object));
        rejected(NoSuchFieldError.class,()->NativeTestBridge.accessField(4,false));
        rejected(NoSuchFieldError.class,()->NativeTestBridge.accessField(13,false));
        Field wide=cls.getField("i"),reference=cls.getField("si");
        assertEquals(long.class,wide.getType());assertEquals(Object.class,reference.getType());
        assertEquals(0L,wide.getLong(object));assertNull(reference.get(null));
        long value=10_000_000_001L;Object token=new byte[31];wide.setLong(object,value);reference.set(null,token);
        assertEquals(value+1,NativeTestBridge.accessChangedLongField(object,value+1));
        VarHandle wideHandle=MethodHandles.lookup().findVarHandle(cls,"i",long.class);
        assertEquals(value+1,(long)wideHandle.get(object));
        apply("field-types2",cls);
        rejected(NoSuchFieldError.class,()->wide.getLong(object));rejected(NoSuchFieldError.class,()->reference.get(null));
        rejected(NoSuchFieldError.class,()->wideHandle.get(object));
        Field newReference=cls.getField("i"),newWide=cls.getField("si");
        assertNull(newReference.get(object));assertEquals(0L,newWide.getLong(null));
        newReference.set(object,token);newWide.setLong(null,value);System.gc();
        assertSame(token,newReference.get(object));assertEquals(value,newWide.getLong(null));
        assertSame(cls,object.getClass());assertEquals(identity,System.identityHashCode(object));
        assertEquals(101,object.keep);assertEquals(102,FieldSubject.staticKeep);assertEquals(104,object.compute());
        report.put("oldReflectionAndJniRetired",true).put("freshLongJniAndVarHandle",true).put("oldObjectGcState",true);
    }); }
    private void fieldStorageEvolutionRequired() throws Throwable { required("field-staticness",()->{
        StorageSubject object=new StorageSubject();object.anchor=71;int identity=System.identityHashCode(object);
        Class<?> cls=object.getClass();String[] names={"value","count","reference","shared"};Field[] old=new Field[4];
        for(int i=0;i<4;i++)old[i]=cls.getField(names[i]);StorageProbe.retain(0,object,false);
        apply("storage-flip",cls);
        for(int i=0;i<4;i++) {
            final int slot=i;rejected(NoSuchFieldError.class,()->old[slot].get(object));
            rejected(NoSuchFieldError.class,()->StorageProbe.field(0,slot,false,null));
            rejected(NoSuchFieldError.class,()->StorageProbe.field(0,slot,true,null));
        }
        Field value=cls.getField("value"),count=cls.getField("count"),reference=cls.getField("reference"),shared=cls.getField("shared");
        assertTrue(Modifier.isStatic(value.getModifiers()));assertFalse(Modifier.isStatic(count.getModifiers()));
        assertTrue(Modifier.isStatic(reference.getModifiers()));assertFalse(Modifier.isStatic(shared.getModifiers()));
        assertEquals(0,value.getInt(null));assertEquals(0L,count.getLong(object));assertNull(reference.get(null));assertNull(shared.get(object));
        StorageProbe.retain(1,object,true);Object token=new byte[29];
        for(int i=0;i<4;i++){StorageProbe.field(1,i,true,token);assertEquals(i==0?45:i==1?9_000_000_089L:1,StorageProbe.field(1,i,false,token));}
        System.gc();assertSame(token,reference.get(null));assertSame(token,shared.get(object));
        assertEquals(71,object.anchor);assertEquals(71,object.stable());assertEquals(identity,System.identityHashCode(object));
        apply("storage-original",cls);
        for(int i=0;i<4;i++){final int slot=i;rejected(NoSuchFieldError.class,()->StorageProbe.field(1,slot,false,token));}
        assertEquals(0,cls.getField("value").getInt(object));assertEquals(0L,cls.getField("count").getLong(null));
        assertEquals(71,object.anchor);assertEquals(identity,System.identityHashCode(object));StorageProbe.release();
        report.put("bothStorageDirections",true).put("retiredJniReadWriteChecks",12).put("freshJniFields",4).put("oldObjectGcState",true);
    }); }
    private void methodSignatureEvolutionRequired() throws Throwable { required("method-signature",()->{
        apply("add",Evolving.class);Evolving object=new Evolving(31);int identity=System.identityHashCode(object);
        Field added=Evolving.class.getField("added");added.setLong(object,123);
        Method old=Evolving.class.getMethod("addedMethod");
        MethodHandle handle=MethodHandles.lookup().unreflect(old);NativeTestBridge.retainRemovalMethod(object);
        apply("method-signature",Evolving.class);
        rejected(NoSuchMethodError.class,()->old.invoke(object));rejected(NoSuchMethodError.class,()->handle.invoke(object));
        for(int i=0;i<4;i++){final int mode=i;rejected(NoSuchMethodError.class,()->NativeTestBridge.invokeRemovalMethod(mode));}
        Method fresh=Evolving.class.getMethod("addedMethod",long.class);
        assertEquals(159L,((Long)fresh.invoke(object,5L)).longValue());
        assertEquals(159L,NativeTestBridge.invokeSignatureMethod(object,5L));
        System.gc();assertEquals(31,object.value);assertEquals(123L,added.getLong(object));
        assertEquals(36,object.compute());assertEquals(identity,System.identityHashCode(object));
        report.put("oldReflectionMethodHandleJniRetired",true).put("freshSignatureReflectionAndJni",true).put("oldObjectGcState",true);
    }); }
    private void nativeJavaTransitionsRequired() throws Throwable { required("native-java",()->{
        NativeShape object=new NativeShape();Object marker=new byte[17],shared=new byte[19],token=new byte[23];
        object.marker=marker;NativeShape.shared=shared;int identity=System.identityHashCode(object);
        Method wide=NativeShape.class.getMethod("wide",long.class,double.class,Object.class);
        Method reference=NativeShape.class.getMethod("reference",Object.class),staticReference=NativeShape.class.getMethod("staticReference",Object.class);
        MethodHandle handle=MethodHandles.lookup().unreflect(wide);NativeShapeProbe.register(NativeShape.class);NativeShapeProbe.retain(object);
        try {
            long x=9_000_000_007L;double y=-43.75;
            for(int generation=0;generation<3;generation++) {
                boolean java=generation==1;
                if(generation>0){apply(java?"native-shape-java":"native-shape-original",NativeShape.class);if(!java){
                    rejected(UnsatisfiedLinkError.class,()->wide.invoke(null,x,y,token));NativeShapeProbe.register(NativeShape.class);
                }}
                assertEquals(!java,Modifier.isNative(NativeShape.class.getMethod("wide",long.class,double.class,Object.class).getModifiers()));
                for(Object argument:new Object[]{token,null}) {
                    long expected=x-43+(java?200:100)+(argument==null?0:1);
                    assertEquals(expected,((Long)wide.invoke(null,x,y,argument)).longValue());
                    assertEquals(expected,(long)handle.invokeWithArguments(x,y,argument));
                    assertSame(java?marker:argument,reference.invoke(object,argument));
                    assertSame(java?shared:argument,staticReference.invoke(null,argument));
                    for(int mode=0;mode<3;mode++) {
                        assertEquals(expected,NativeShapeProbe.wide(mode,x,y,argument));
                        assertSame(java?marker:argument,NativeShapeProbe.reference(mode,false,argument));
                        assertSame(java?shared:argument,NativeShapeProbe.reference(mode,true,argument));
                    }
                    assertEquals(expected,NativeShape.wide(x,y,argument));assertSame(java?marker:argument,object.reference(argument));
                }
                System.gc();assertEquals(identity,System.identityHashCode(object));assertSame(marker,object.marker);assertSame(shared,NativeShape.shared);
            }
            report.put("nativeJavaNative",true).put("cachedReflectionMethodHandleJni",true).put("mixedWideFpReferenceArguments",true).put("oldObjectGcState",true);
        } finally {NativeShapeProbe.release();}
    }); }
    private void hierarchyAdditionRequired() throws Throwable { required("hierarchy-addition",()->{
        HierarchySubject object=new HierarchySubject(),alias=object;
        HierarchyChild child=new HierarchyChild();
        HierarchySubject[] array={object,child};
        Class<?> cls=object.getClass(),childClass=child.getClass();
        int identity=System.identityHashCode(object),childIdentity=System.identityHashCode(child);
        Object own=new byte[31],baseRef=new byte[37];
        object.root=9_000_000_089L;object.base=17;object.value=57;
        object.ownRef=own;object.baseRef=baseRef;HierarchySubject.counter=777;
        child.root=8_000_000_089L;child.base=27;child.value=67;
        Method compute=cls.getMethod("compute"),inherited=HierarchyBase.class.getMethod("inherited");
        MethodHandle computeHandle=MethodHandles.lookup().unreflect(compute);
        MethodHandle inheritedHandle=MethodHandles.lookup().unreflect(inherited);
        Field ownField=cls.getField("value"),baseField=cls.getField("base"),rootField=cls.getField("root");
        HierarchyProbe.retain(object);
        HierarchyProbe.retainInterfaceMethod(HierarchyTag.class);
        Method tag=HierarchyTag.class.getMethod("tag");
        MethodHandle tagHandle=MethodHandles.lookup().unreflect(tag);
        int interfaceJniChecks=0;
        try {
            assertFalse(HierarchyTag.class.isInstance(object));
            assertEquals(75,object.compute());assertEquals(37,HierarchyProbe.invoke(false));
            String[] variants={"hier-interface","production-hierarchy-parent"};
            for(int generation=0;generation<variants.length;generation++) {
                int constructors=HierarchyInserted.constructions;
                apply(variants[generation],cls);
                assertSame(generation==0?HierarchyBase.class:HierarchyInserted.class,cls.getSuperclass());
                assertTrue(HierarchyTag.class.isAssignableFrom(cls));
                assertTrue(HierarchyTag.class.isInstance(child));
                assertEquals(59+generation,((HierarchyTag)(Object)object).tag());
                assertEquals(11,((HierarchyTag)(Object)object).defaultValue());
                assertEquals(59+generation,((Integer)tag.invoke(object)).intValue());
                assertEquals(59+generation,(int)tagHandle.invokeWithArguments(object));
                for(int form=0;form<3;form++){
                    assertEquals(59+generation,HierarchyProbe.interfaceJniInvokeForm(object,form,false));
                    interfaceJniChecks++;
                }
                if(generation==1)assertEquals(22,((HierarchyMore)(Object)object).more());
                int expected=76+generation,dispatch=generation==0?37:57;
                assertEquals(expected,object.compute());assertEquals(expected,((Integer)compute.invoke(object)).intValue());
                assertEquals(expected,(int)computeHandle.invokeWithArguments(object));
                assertEquals(dispatch,((Integer)inherited.invoke(object)).intValue());
                assertEquals(dispatch,(int)inheritedHandle.invokeWithArguments(object));
                assertEquals(dispatch,object.throughSuper());
                assertEquals(dispatch,HierarchyProbe.invoke(false));assertEquals(37,HierarchyProbe.invoke(true));
                long[] fields=HierarchyProbe.fields();assertEquals(9_000_000_089L,fields[0]);
                assertEquals(17,fields[1]);assertEquals(57,fields[2]);
                assertEquals(57,ownField.getInt(object));assertEquals(17,baseField.getInt(object));
                assertEquals(9_000_000_089L,rootField.getLong(object));
                assertEquals(constructors,HierarchyInserted.constructions);
                assertEquals(generation==1,HierarchyInserted[].class.isAssignableFrom(array.getClass()));
                if(generation==1) {
                    assertEquals(0.0,HierarchyInserted.class.getField("added").getDouble(object));
                    assertNull(HierarchyInserted.class.getField("addedRef").get(object));
                    assertEquals(0,HierarchyInserted.class.getField("value").getInt(object));
                    assertEquals(0.0,HierarchyInserted.class.getField("added").getDouble(child));
                    HierarchyInserted.class.getField("added").setDouble(object,4.25);
                    HierarchyInserted.class.getField("addedRef").set(object,own);
                }
                System.gc();assertSame(alias,object);assertSame(object,array[0]);assertSame(child,array[1]);
                assertSame(cls,object.getClass());assertSame(childClass,child.getClass());
                assertSame(cls,childClass.getSuperclass());assertEquals(identity,System.identityHashCode(object));
                assertEquals(childIdentity,System.identityHashCode(child));assertSame(own,object.ownRef);
                assertSame(baseRef,object.baseRef);assertEquals(777,HierarchySubject.counter);
                assertEquals(8_000_000_089L,child.root);assertEquals(27,child.base);
                assertEquals(67,child.value);assertEquals(8_888_888_888L,child.child);
                if(generation==1){
                    assertEquals(4.25,HierarchyInserted.class.getField("added").getDouble(object));
                    assertSame(own,HierarchyInserted.class.getField("addedRef").get(object));
                }
            }
            report.put("actualInterfaceAndParentAddition",true).put("inheritedFieldsPreservedAndAddedZeroed",true)
                .put("cachedVirtualReflectionMethodHandleJni",true).put("interfaceDispatch",true)
                .put("oldObjectAndSubclassGcIdentity",true).put("constructorsNotReplayed",true)
                .put("interfaceJniChecks",interfaceJniChecks);
        } finally {HierarchyProbe.releaseInterfaceMethod();HierarchyProbe.release();}
    }); }
    private void receiverRejected(Action action) throws Throwable {
        Throwable caught=null;
        try {action.run();}catch(InvocationTargetException error){caught=error.getCause();}catch(Throwable error){caught=error;}
        assertTrue("Invalid ancestor receiver must reject before access, got "+caught,
            caught instanceof IncompatibleClassChangeError || caught instanceof ClassCastException
            || caught instanceof IllegalArgumentException
            || caught instanceof IllegalStateException);
    }
    private void hierarchyNarrowingRequired() throws Throwable { required("hierarchy-narrowing",()->{
        NarrowSubject object=new NarrowSubject();object.own=41;
        Class<?> cls=object.getClass();int identity=System.identityHashCode(object);
        NarrowStorage storage=new NarrowStorage();storage.legal=object;storage.legalArray=new Object[]{object};
        NarrowParent validParent=new NarrowParent();validParent.ancestor=71;
        Field ancestor=NarrowParent.class.getField("ancestor");
        Method inherited=NarrowParent.class.getMethod("inherited");
        MethodHandle getter=MethodHandles.lookup().unreflectGetter(ancestor);
        MethodHandle method=MethodHandles.lookup().unreflect(inherited);
        assertEquals(13,HierarchyProbe.narrowJniRead(object));assertEquals(14,HierarchyProbe.narrowJniInvoke(object));
        apply("hier-narrow-simple",cls);
        assertSame(Object.class,cls.getSuperclass());assertFalse(NarrowParent.class.isInstance(object));
        assertEquals(43,object.compute());assertEquals(41,object.own);
        receiverRejected(()->ancestor.getInt(object));receiverRejected(()->inherited.invoke(object));
        receiverRejected(()->getter.invokeWithArguments(object));receiverRejected(()->method.invokeWithArguments(object));
        receiverRejected(()->HierarchyProbe.narrowJniRead(object));
        receiverRejected(()->HierarchyProbe.narrowJniInvoke(object));
        receiverRejected(()->HierarchyProbe.narrowJniInvokeNonvirtual(object));
        for(int form=0;form<4;form++){final int selected=form;receiverRejected(()->HierarchyProbe.narrowJniInvokeForm(object,selected));}
        // A blanket ancestor-access ban would pass the negative checks but is wrong.
        assertEquals(71,ancestor.getInt(validParent));assertEquals(72,((Integer)inherited.invoke(validParent)).intValue());
        assertEquals(71,(int)getter.invokeWithArguments(validParent));
        assertEquals(72,(int)method.invokeWithArguments(validParent));
        assertEquals(71,HierarchyProbe.narrowJniRead(validParent));assertEquals(72,HierarchyProbe.narrowJniInvoke(validParent));
        System.gc();assertSame(object,storage.legal);assertSame(object,storage.legalArray[0]);
        assertSame(cls,object.getClass());assertEquals(identity,System.identityHashCode(object));
        report.put("legalTypedStorageNarrowing",true).put("retiredAncestorReceiverChecks",true)
            .put("unrelatedAncestorReceiverStillWorks",true);
        apply("hier-narrow-restore",cls);object.ancestor=61;
        int rejects=0;
        try {
            for(int cell=0;cell<3;cell++) {
                if(cell==0)storage.ancestor=object;
                if(cell==1)NarrowStorage.staticAncestor=object;
                if(cell==2)storage.ancestorArray=new NarrowParent[]{object};
                stage("illegal-typed-storage-"+cell+".before");
                int result=NativeBridge.apply(2,new Class<?>[]{cls},new byte[][]{asset("hier-narrow-simple",cls)});
                report.put("illegalStorageCode"+cell,result);
                assertTrue("Illegal retained ancestor cell must reject",result!=0);
                assertSame(NarrowParent.class,cls.getSuperclass());assertTrue(NarrowParent.class.isInstance(object));
                assertEquals(61,object.ancestor);assertEquals(41,object.own);assertEquals(42,object.compute());
                assertEquals(62,HierarchyProbe.narrowJniInvoke(object));assertEquals(61,ancestor.getInt(object));
                if(cell==0)assertSame(object,storage.ancestor);
                if(cell==1)assertSame(object,NarrowStorage.staticAncestor);
                if(cell==2)assertSame(object,storage.ancestorArray[0]);
                System.gc();assertSame(object,storage.legal);assertSame(object,storage.legalArray[0]);
                assertSame(cls,object.getClass());assertEquals(identity,System.identityHashCode(object));
                storage.ancestor=null;NarrowStorage.staticAncestor=null;storage.ancestorArray=null;rejects++;
            }
            // All forbidden cells are gone: refusing every narrowing is not support.
            apply("hier-narrow-simple",cls);System.gc();
            assertSame(Object.class,cls.getSuperclass());assertEquals(43,object.compute());assertEquals(41,object.own);
            receiverRejected(()->HierarchyProbe.narrowJniRead(object));
            receiverRejected(()->HierarchyProbe.narrowJniInvoke(object));
            assertSame(object,storage.legal);assertSame(object,storage.legalArray[0]);
            assertEquals(identity,System.identityHashCode(object));
            report.put("illegalTypedStorageAtomicRejection",true).put("illegalTypedStorageChecks",rejects)
                .put("legalNarrowingAfterRejectedAttempts",true).put("oldObjectGcState",true);
        } finally {storage.ancestor=null;NarrowStorage.staticAncestor=null;storage.ancestorArray=null;}
    }); }
}
