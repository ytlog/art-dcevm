package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.TextView;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Optional, inert foreground window for controlled benchmark conditions. */
public final class HookBenchmarkActivity extends Activity {
    private final CountDownLatch ready = new CountDownLatch(1);
    private volatile boolean resumed;
    private volatile boolean focused;
    private volatile int interruptions;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        TextView label = new TextView(this);
        label.setText("Local runtime benchmark running");
        label.setPadding(32, 32, 32, 32);
        setContentView(label);
    }

    @Override public void onResume() {
        super.onResume();
        resumed = true;
        if (focused) ready.countDown();
    }

    @Override public void onPause() {
        interruptions++;
        resumed = false;
        super.onPause();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (focused && !hasFocus) interruptions++;
        focused = hasFocus;
        if (hasFocus && resumed) ready.countDown();
    }

    boolean awaitReady() throws InterruptedException { return ready.await(5, TimeUnit.SECONDS); }
    boolean isReady() { return resumed && focused; }
    int interruptions() { return interruptions; }
}
