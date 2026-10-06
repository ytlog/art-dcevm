package dev.artdcevm;
import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.widget.ScrollView;
import android.widget.TextView;
import dev.artdcevm.testing.ConformanceSuite;
import dev.artdcevm.testing.PublicJvmtiSuite;
public final class MainActivity extends Activity {
    private static boolean started;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text = new TextView(this);
        text.setPadding(24, 24, 24, 24);
        text.setTextSize(14);
        text.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this); scroll.addView(text); setContentView(scroll);
        if (started) { text.setText("本进程已运行测试。请先强制停止应用，再重新启动测试。"); return; }
        started = true;
        boolean privateBackend = Build.VERSION.SDK_INT == 33 && "arm64-v8a".equals(Build.SUPPORTED_ABIS[0]);
        text.setText(privateBackend ? "正在测试 Android 13 ART 热替换…" : "正在测试公开 JVMTI 热替换…");
        new Thread(() -> {
            String result = (privateBackend ? new ConformanceSuite(getApplicationContext()).run()
                    : new PublicJvmtiSuite(getApplicationContext()).run()).toString();
            runOnUiThread(() -> text.setText(result));
        }, "dcevm-conformance").start();
    }
}
