package com.github.ytlog.artdcevm.testing;

import android.os.Process;
import android.util.Log;

/** Read-only phase breadcrumbs; call only at Java suite boundaries. */
public final class TestPhaseLog {
    private final String runId;
    private final int pid=Process.myPid();
    public TestPhaseLog(String runId) {
        if(runId==null||!runId.matches("[0-9a-f]{32}"))throw new IllegalArgumentException("Malformed test run ID");
        this.runId=runId;
    }
    public void mark(String phase) {
        // Phase names are fixed source literals; avoid JSON escaping ambiguity.
        if(!phase.matches("[a-z0-9.-]+"))throw new IllegalArgumentException("Malformed phase name");
        Log.i("ArtDcevmPhase","{\"runId\":\""+runId+"\",\"pid\":"+pid+",\"phase\":\""+phase+"\"}");
    }
}
