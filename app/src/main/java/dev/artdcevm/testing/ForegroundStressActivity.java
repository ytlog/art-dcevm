package dev.artdcevm.testing;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.view.Choreographer;
import android.view.View;
import dev.artdcevm.internal.PatchRequest;
import dev.artdcevm.internal.PatchResult;
import dev.artdcevm.internal.RuntimeBackend;
import dev.artdcevm.engine.BackendRegistry;
import dalvik.system.InMemoryDexClassLoader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONArray;
import org.json.JSONObject;

/** Regression gate: real UI frames overlap structural commits on a worker. */
@android.annotation.TargetApi(33)
public final class ForegroundStressActivity extends Activity implements Choreographer.FrameCallback {
    private final AtomicInteger frames = new AtomicInteger(), draws = new AtomicInteger();
    private final AtomicInteger renderedBuffers = new AtomicInteger();
    private final AtomicInteger focusLosses = new AtomicInteger();
    private final CountDownLatch firstDraw = new CountDownLatch(1);
    private final Object frameGate = new Object();
    private volatile boolean running = true;
    private volatile boolean windowFocused;
    private volatile boolean everFocused;
    private boolean requireDrawing;
    private View view;
    private boolean virtualDrawing;
    private boolean startupRace;
    private ImageReader images;
    private VirtualDisplay virtualDisplay;
    private Presentation presentation;
    private static native boolean enableDeoptTracing();
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        System.loadLibrary("artdcevm_test_support");
        setShowWhenLocked(true);
        setTurnScreenOn(true);
        requireDrawing = !getIntent().getBooleanExtra("callbacksOnly", false);
        virtualDrawing = getIntent().getBooleanExtra("virtualDisplay", false);
        startupRace = getIntent().getBooleanExtra("startupRace", false);
        if (virtualDrawing) {
            // A private app-owned display exercises ViewRoot/renderer work while
            // the physical display is locked. It grants no lockscreen access.
            images = ImageReader.newInstance(720, 480, PixelFormat.RGBA_8888, 3);
            images.setOnImageAvailableListener(reader -> {
                try (Image image = reader.acquireLatestImage()) {
                    if (image != null) renderedBuffers.incrementAndGet();
                }
            }, new Handler(Looper.getMainLooper()));
            DisplayManager displays = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
            virtualDisplay = displays.createVirtualDisplay("ART DCEVM render test", 720, 480, 160,
                    images.getSurface(), DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION);
            if (virtualDisplay == null) throw new IllegalStateException("Virtual display unavailable");
            presentation = new Presentation(this, virtualDisplay.getDisplay());
            view = createDrawingView(presentation.getContext());
            presentation.setContentView(view); presentation.show();
            android.widget.TextView label = new android.widget.TextView(this);
            label.setText("ART DCEVM: app-owned virtual display stress"); setContentView(label);
        } else {
            view = createDrawingView(this); setContentView(view);
        }
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Choreographer.getInstance().postFrameCallback(this);
        boolean source = getIntent().getBooleanExtra("source", false);
        int rounds = Math.max(1, Math.min(2000, getIntent().getIntExtra("rounds", 48)));
        new Thread(() -> runStress(source, rounds), "foreground-redefine").start();
    }
    private View createDrawingView(Context context) {
        return new View(context) {
            private final Paint paint = new Paint();
            @Override protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                int n = draws.incrementAndGet();
                canvas.drawColor(0xffeeeeee);
                paint.setColor(0xff205090); paint.setTextSize(40);
                canvas.drawCircle(60 + n % 500, 100, 35, paint);
                canvas.drawText("ART foreground stress: " + n, 30, 200, paint);
                firstDraw.countDown();
                synchronized (frameGate) { frameGate.notifyAll(); }
            }
        };
    }
    @Override public void doFrame(long timestamp) {
        frames.incrementAndGet();
        synchronized (frameGate) { frameGate.notifyAll(); }
        view.invalidate();
        if (running) Choreographer.getInstance().postFrameCallback(this);
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused && everFocused) focusLosses.incrementAndGet();
        if (focused) everFocused = true;
        windowFocused = focused;
        synchronized (frameGate) { frameGate.notifyAll(); }
    }
    @Override protected void onDestroy() {
        running = false;
        Choreographer.getInstance().removeFrameCallback(this);
        synchronized (frameGate) { frameGate.notifyAll(); }
        if (presentation != null) presentation.dismiss();
        if (virtualDisplay != null) virtualDisplay.release();
        if (images != null) images.close();
        super.onDestroy();
    }
    private void runStress(boolean source, int rounds) {
        JSONObject report = new JSONObject();
        JSONArray checks = new JSONArray();
        try {
            report.put("uid", Process.myUid()).put("source", source).put("rounds", rounds)
                    .put("display", virtualDrawing ? "app-owned virtual display" : "physical display")
                    .put("startupRace", startupRace)
                    .put("drawRequired", requireDrawing).put("completed", false).put("passed", false).put("fullDcevmComplete", false).put("checks", checks);
            save(report);
            if (requireDrawing && !startupRace && !firstDraw.await(10, TimeUnit.SECONDS)) throw new AssertionError("No foreground drawing");
            RuntimeBackend backend = BackendRegistry.open(getApplicationContext());
            if (source && !backend.enableEvolution().installed()) throw new AssertionError("Source unavailable");
            if (source && getIntent().getBooleanExtra("deoptTracing", true)
                    && !enableDeoptTracing()) throw new AssertionError("Deopt tracing unavailable");
            // Stock mode intentionally does not install any validation/source hook.
            report.put("capabilities", backend.capabilities()); save(report);
            byte[] original = asset("ui-original/dev/artdcevm/fixture/UiSubject.dex");
            byte[] added = asset("ui-added/dev/artdcevm/fixture/UiSubject.dex");
            for (int i = 0; i < rounds; ++i) {
                int before = progress();
                if (!(startupRace && i == 0)) {
                    synchronized (frameGate) {
                        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
                        while (running && progress() == before && System.nanoTime() < deadline) frameGate.wait(100);
                    }
                    if (!running || progress() == before) throw new AssertionError(
                            !virtualDrawing && !windowFocused
                                    ? "Physical window lost focus; drawing paused"
                                    : "Foreground drawing stalled while focused");
                }
                ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(original), getClassLoader());
                Class<?> cls = loader.loadClass("dev.artdcevm.fixture.UiSubject");
                Object object = cls.getConstructor().newInstance();
                Object alias = object;
                int hash = System.identityHashCode(object);
                Field value = cls.getField("value"); value.setInt(object, i);
                Method compute = cls.getMethod("compute");
                report.put("phase", "commit_" + i).put("frames", frames.get()).put("draws", draws.get())
                        .put("renderedBuffers", renderedBuffers.get()); save(report);
                int drawsBeforeCommit = draws.get();
                PatchResult result = backend.apply(new PatchRequest(source ? PatchRequest.Operation.EVOLVE_SCHEMA
                        : PatchRequest.Operation.REDEFINE_STRUCTURE, new Class<?>[]{cls}, new byte[][]{added}));
                if (!result.installed()) throw new AssertionError("Commit " + i + ": " + result);
                boolean ok = object == alias && object.getClass() == cls && System.identityHashCode(object) == hash
                        && value.getInt(object) == i && (Integer) compute.invoke(object) == i + 2
                        && cls.getField("added").getLong(object) == 0;
                checks.put(new JSONObject().put("round", i).put("passed", ok).put("frames", frames.get()).put("draws", draws.get())
                        .put("drawsBeforeCommit", drawsBeforeCommit).put("windowFocused", windowFocused)
                        .put("focusLosses", focusLosses.get()));
                if (!ok) throw new AssertionError("State mismatch at " + i);
                if (i % 4 == 3) System.gc();
                if (i % 50 == 49 || i == rounds - 1) {
                    Runtime runtime = Runtime.getRuntime();
                    report.put("memorySampleRound", i + 1)
                            .put("nativeHeapBytes", Debug.getNativeHeapAllocatedSize())
                            .put("javaUsedBytes", runtime.totalMemory() - runtime.freeMemory());
                }
                save(report);
            }
            if (requireDrawing && !firstDraw.await(10, TimeUnit.SECONDS)) throw new AssertionError("No drawing after startup commits");
            if (!virtualDrawing && requireDrawing && (!everFocused || !windowFocused || focusLosses.get() != 0))
                throw new AssertionError("Physical window did not retain focus throughout the test");
            if (virtualDrawing && renderedBuffers.get() == 0) throw new AssertionError("No rendered virtual display buffers");
            report.put("passed", true).put("capabilitiesAfter", backend.capabilities());
        } catch (Throwable error) {
            try { report.put("fatal", Log.getStackTraceString(error)); } catch (Exception ignored) { }
        }
        try { report.put("completed", true).put("frames", frames.get()).put("draws", draws.get())
                .put("renderedBuffers", renderedBuffers.get()).put("windowFocused", windowFocused)
                .put("everFocused", everFocused).put("focusLosses", focusLosses.get()); save(report); }
        catch (Exception ignored) { }
    }
    private int progress() { return requireDrawing ? draws.get() : frames.get(); }
    private byte[] asset(String path) throws Exception {
        try (InputStream input = getAssets().open(path); ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096]; int n;
            while ((n = input.read(buffer)) >= 0) bytes.write(buffer, 0, n);
            return bytes.toByteArray();
        }
    }
    private void save(JSONObject report) throws Exception {
        try (FileOutputStream stream = new FileOutputStream(new File(getFilesDir(), "foreground-stress.json"))) {
            stream.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
        }
    }
}
