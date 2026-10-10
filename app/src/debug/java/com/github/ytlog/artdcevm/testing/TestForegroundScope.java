package com.github.ytlog.artdcevm.testing;

import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import org.json.JSONObject;

/** Optional same-process foreground lifetime; grants no runtime capability. */
final class TestForegroundScope {
    private final Instrumentation instrumentation;
    private final JSONObject report;
    private final TestPhaseLog phases;
    private final boolean requested;
    private RuntimeAccessForegroundActivity carrier;
    private int initialPauses;

    TestForegroundScope(Instrumentation instrumentation, Bundle arguments,
                        JSONObject report, TestPhaseLog phases) throws Exception {
        this.instrumentation=instrumentation;this.report=report;this.phases=phases;
        requested="true".equals(arguments.getString("foreground"));
        report.put("foregroundRequested",requested);
    }
    void start() throws Exception {
        if(!requested)return;
        Intent intent=new Intent(instrumentation.getTargetContext(),RuntimeAccessForegroundActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        phases.mark("foreground.start-activity.before");
        carrier=(RuntimeAccessForegroundActivity)instrumentation.startActivitySync(intent);
        phases.mark("foreground.start-activity.after");
        phases.mark("foreground.wait-idle.before");instrumentation.waitForIdleSync();
        phases.mark("foreground.wait-idle.after");initialPauses=carrier.carrierPauses();
        report.put("foregroundActivity",carrier.getClass().getName())
                .put("foregroundInstrumentationPid",Process.myPid()).put("foregroundActivityPid",carrier.carrierPid())
                .put("foregroundResumedBefore",carrier.carrierResumed());
        if(carrier.carrierPid()!=Process.myPid()||!carrier.carrierResumed())
            throw new AssertionError("Foreground carrier must share the resumed instrumentation process");
    }
    boolean finish() throws Exception {
        if(!requested)return true;
        boolean resumed=carrier!=null&&carrier.carrierResumed();
        int pauses=carrier==null?-1:carrier.carrierPauses()-initialPauses;
        boolean samePid=carrier!=null&&carrier.carrierPid()==Process.myPid();
        boolean finished=false;
        report.put("foregroundResumedAfter",resumed).put("foregroundPauseCount",pauses);
        if(carrier!=null){
            try {
                final RuntimeAccessForegroundActivity closing=carrier;
                phases.mark("foreground.finish.before");instrumentation.runOnMainSync(closing::finish);
                finished=closing.awaitDestroyed();phases.mark("foreground.finish.after");
            } catch(Throwable error) {
                report.put("foregroundCleanupError",String.valueOf(error));
                if(error instanceof InterruptedException)Thread.currentThread().interrupt();
            }
        }
        report.put("foregroundFinished",finished);
        return samePid&&resumed&&pauses==0&&finished;
    }
}
