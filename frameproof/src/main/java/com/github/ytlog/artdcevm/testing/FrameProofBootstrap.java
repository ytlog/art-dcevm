package com.github.ytlog.artdcevm.testing;

import android.os.Debug;
import android.os.SystemClock;
import java.lang.reflect.Method;
import org.json.JSONObject;

/** Owned process bootstrap: public JNI/GC and named original JIT observations only. */
public final class FrameProofBootstrap {
    private static native String begin(Method[] methods,int[] shape,Class<?> carrier);
    private static native boolean ready();
    private static native String finish(long completedGcDelta);
    private static native void cancel();
    private FrameProofBootstrap() { }
    private static long gcCount() {
        String count=Debug.getRuntimeStat("art.gc.gc-count");
        if(count==null)throw new AssertionError("Missing public completed GC count");
        long value=Long.parseLong(count);
        if(value<0)throw new AssertionError("Negative public completed GC count");
        return value;
    }
    public static synchronized JSONObject run(String apkPath)throws Exception {
        System.loadLibrary("artdcevm");
        Class<?>[] args={int.class,Object.class,int.class,Object.class,int.class,Object.class};
        Method[] methods={OwnedActiveFrameFixture.class.getDeclaredMethod("carrier",args),
            OwnedActiveFrameFixture.class.getDeclaredMethod("inlinee1",args),OwnedActiveFrameFixture.class.getDeclaredMethod("inlinee2",args),
            OwnedActiveFrameFixture.class.getDeclaredMethod("awaitGate"),OwnedActiveFrameFixture.class.getDeclaredMethod("marker")};
        int[] shape=ActiveFrameDexOracle.read(apkPath,methods);
        JSONObject bootstrap;
        try {
            String error=begin(methods,shape,OwnedActiveFrameFixture.class);
            if(error==null||!error.isEmpty())throw new AssertionError("Frame JNI bootstrap: "+error);
            Object a=new Object(),b=new Object(),c=new Object();
            int expected=31+43+59+System.identityHashCode(a)+System.identityHashCode(b)+System.identityHashCode(c);
            for(int i=0;i<100000;++i)
                if(OwnedActiveFrameFixture.carrier(31,a,43,b,59,c)!=expected)throw new AssertionError("Carrier bootstrap warm values changed");
            long before=gcCount();System.gc();long end=SystemClock.elapsedRealtime()+30000;
            long after=gcCount();
            while((after<=before||!ready())&&SystemClock.elapsedRealtime()<end){SystemClock.sleep(10);after=gcCount();}
            String result=finish(after-before);
            if(result==null)throw new AssertionError("Missing native bootstrap proof");
            bootstrap=new JSONObject(result).put("warmCalls",100000);
        } finally {cancel();}
        if(!bootstrap.getBoolean("passed"))
            return new JSONObject().put("passed",false).put("bootstrap",bootstrap).put("error","Missing real pure-JNI Runtime/JIT/GC prerequisites");
        // Same instrumentation thread owns the TLS native snapshot; no public backend init.
        JSONObject frame=OwnedActiveFrameCalibration.run(apkPath,true);
        return new JSONObject().put("passed",frame.getBoolean("passed")).put("bootstrap",bootstrap).put("activeFrames",frame);
    }
}
