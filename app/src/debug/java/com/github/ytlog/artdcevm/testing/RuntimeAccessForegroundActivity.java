package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.os.Bundle;
import android.os.Process;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.TextView;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Keeps the owned-runtime instrumentation foreground; owns no runtime/backend. */
public final class RuntimeAccessForegroundActivity extends Activity {
    private volatile boolean resumed;
    private volatile int pauses;
    private final int processId = Process.myPid();
    private final CountDownLatch destroyed = new CountDownLatch(1);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setShowWhenLocked(true);
        setTurnScreenOn(true);
        TextView label = new TextView(this);
        label.setGravity(Gravity.CENTER);
        label.setTextSize(22);
        label.setText("ART DCEVM runtime test\nRunning in foreground · PID " + processId);
        setContentView(label);
    }
    @Override public void onResume() { super.onResume(); resumed = true; }
    @Override public void onPause() { resumed = false; pauses++; super.onPause(); }
    @Override public void onDestroy() { super.onDestroy(); destroyed.countDown(); }
    boolean carrierResumed() { return resumed; }
    int carrierPauses() { return pauses; }
    int carrierPid() { return processId; }
    boolean awaitDestroyed() throws InterruptedException { return destroyed.await(5, TimeUnit.SECONDS); }
}
