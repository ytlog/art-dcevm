package com.github.ytlog.artdcevm.testing;
import java.lang.reflect.Method;
import org.json.JSONObject;

/** Standalone debug fixture; not an evolution capability or current suite gate. */
public final class OwnedActiveFrameCalibration {
    private static native String prepareCarrier(Method[] methods,int[] shape);
    private static native String armGate();
    private static native boolean waitEntered(int timeoutMillis);
    private static native void releaseGate();
    private static native String prove(Method[] methods,int[] dexShape,Object[] roots,int[] ints);
    private static native String provePhysical(Method[] methods,int[] dexShape,Object[] roots,int[] ints);
    private OwnedActiveFrameCalibration(){ }
    public static JSONObject run(String apkPath)throws Exception {return run(apkPath,false);}
    public static JSONObject run(String apkPath,boolean requestOptimizingCompilation)throws Exception {
        return runCallers(apkPath,requestOptimizingCompilation,false);
    }
    public static JSONObject runPhysical(String apkPath)throws Exception {
        return runCallers(apkPath,false,true);
    }
    private static JSONObject runCallers(String apkPath,boolean requestOptimizingCompilation,boolean physical)throws Exception {
        Class<?>[] args={int.class,Object.class,int.class,Object.class,int.class,Object.class};
        Method carrier=OwnedActiveFrameFixture.class.getDeclaredMethod("carrier",args);
        Method[] methods={carrier,OwnedActiveFrameFixture.class.getDeclaredMethod("inlinee1",args),
            OwnedActiveFrameFixture.class.getDeclaredMethod("inlinee2",args),
            OwnedActiveFrameFixture.class.getDeclaredMethod("awaitGate"),OwnedActiveFrameFixture.class.getDeclaredMethod("marker")};
        int[] dex=ActiveFrameDexOracle.read(apkPath,methods);
        int[] integers={31,43,59};Object[] roots={new Object(),new Object(),new Object()};
        Object[] invocation={integers[0],roots[0],integers[1],roots[1],integers[2],roots[2]};
        int expected=integers[0]+integers[1]+integers[2]+System.identityHashCode(roots[0])+System.identityHashCode(roots[1])+System.identityHashCode(roots[2]);
        for(int i=0;i<100000;++i)if((Integer)carrier.invoke(null,invocation)!=expected)throw new AssertionError("Active fixture warmup changed values");
        JSONObject compilation=null;
        if(requestOptimizingCompilation) {
            String result=prepareCarrier(methods,dex);
            if(result==null)throw new AssertionError("Missing owned optimizing compilation report");
            compilation=new JSONObject(result);
            if(!compilation.getBoolean("passed"))throw new AssertionError("Owned carrier compilation: "+result);
        }
        OwnedActiveFrameFixture.retained0=OwnedActiveFrameFixture.retained1=OwnedActiveFrameFixture.retained2=null;
        OwnedActiveFrameFixture.retainedInt0=OwnedActiveFrameFixture.retainedInt1=OwnedActiveFrameFixture.retainedInt2=0;
        final Throwable[] failure={null};final int[] value={0};
        String arm=armGate();if(arm==null||!arm.isEmpty())throw new AssertionError("Active gate initialization: "+arm);
        Thread worker=null;boolean started=false;JSONObject report=null;
        try {
            worker=new Thread(()->{try{value[0]=OwnedActiveFrameFixture.carrier(integers[0],roots[0],integers[1],roots[1],integers[2],roots[2]);}catch(Throwable error){failure[0]=error;}},"OwnedCompiledFrameOracle");
            worker.start();started=true;
            if(!waitEntered(30000))throw new AssertionError("Actual native worker gate was not entered");
            if(OwnedActiveFrameFixture.retained0!=null||OwnedActiveFrameFixture.retained1!=null||OwnedActiveFrameFixture.retained2!=null||OwnedActiveFrameFixture.retainedInt0!=0||OwnedActiveFrameFixture.retainedInt1!=0||OwnedActiveFrameFixture.retainedInt2!=0)
                throw new AssertionError("Fixture wrote live witnesses before native gate release");
            String result=physical?provePhysical(methods,dex,roots,integers):prove(methods,dex,roots,integers);
            if(result==null)throw new AssertionError("Missing active frame report");
            report=new JSONObject(result);
            if(report.getBoolean("passed")&&(report.getInt("frames")!=3||report.getInt("integers")!=3||report.getInt("references")!=3||report.getInt("locationReads")!=6||!report.getBoolean("carrierJitCompiled")||report.getInt("dexPcChecks")!=3||(physical?(report.getInt("physicalMethodMatches")!=3||report.getInt("inlineMethodMatches")!=0):!report.getBoolean("inlineFlagNativeObserved"))))
                throw new AssertionError("Incomplete actual active frame proof: "+result);
            report.put("warmCalls",100000).put("scope",physical?"three owned physical compiled callers with independent DexPc/value/root witnesses; no whole-process frame eligibility":"owned compiled carrier and two inlinee map/value/root oracle; no full evolution");
            report.put("blockedLiveWitnesses",true);
            if(compilation!=null)report.put("compilationControl",compilation);
        } finally {
            releaseGate();if(started)worker.join(30000);
            if(started&&(worker.isAlive()||failure[0]!=null||value[0]!=expected||OwnedActiveFrameFixture.retained0!=roots[0]||OwnedActiveFrameFixture.retained1!=roots[1]||OwnedActiveFrameFixture.retained2!=roots[2]||OwnedActiveFrameFixture.retainedInt0!=integers[0]||OwnedActiveFrameFixture.retainedInt1!=integers[1]||OwnedActiveFrameFixture.retainedInt2!=integers[2]))throw new AssertionError("Active frame gate cleanup/retained result",failure[0]);
        }
        return report.put("retainedLiveWitnesses",true);
    }
}
