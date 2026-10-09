package com.github.ytlog.artdcevm.frameproof;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import com.github.ytlog.artdcevm.testing.FrameProofBootstrap;
import org.json.JSONObject;

/** Same-signature target instrumentation; no Debug.attachJvmtiAgent or public backend. */
public final class FrameProofInstrumentation extends Instrumentation {
    private String runId;
    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);runId=arguments==null?null:arguments.getString("frameRunId");start();
    }
    @Override public void onStart() {
        JSONObject report=new JSONObject();boolean passed=false;
        try {
            Context target=getTargetContext(),test=getContext();PackageManager pm=target.getPackageManager();
            boolean targetNondebug=(target.getApplicationInfo().flags&ApplicationInfo.FLAG_DEBUGGABLE)==0;
            boolean testNondebug=(test.getApplicationInfo().flags&ApplicationInfo.FLAG_DEBUGGABLE)==0;
            boolean sameSignature=pm.checkSignatures(target.getPackageName(),test.getPackageName())==PackageManager.SIGNATURE_MATCH;
            if(runId==null||runId.isEmpty())throw new AssertionError("Missing current frame run ID");
            if(!targetNondebug||!testNondebug||!sameSignature)throw new AssertionError("Independent nondebuggable/same-signature target policy rejected");
            report=FrameProofBootstrap.run(target.getApplicationInfo().sourceDir);
            report.put("targetNondebuggable",targetNondebug).put("instrumentationNondebuggable",testNondebug).put("sameSignature",sameSignature);
            passed=report.getBoolean("passed");
        } catch(Throwable error) {
            try{report.put("passed",false).put("error",error.toString());}catch(Exception ignored){ }
        }
        Bundle result=new Bundle();
        try {report.put("runId",runId).put("protocol",1).put("scope","owned JIT compiled carrier and two inlinees with native/DEXPC/int/reference witnesses; no full evolution");}
        catch(Exception ignored){passed=false;}
        result.putString("frameProof",report.toString());
        result.putString("stream","FRAME_PROOF_REPORT="+report+"\n");
        finish(passed?Activity.RESULT_OK:Activity.RESULT_CANCELED,result);
    }
}
