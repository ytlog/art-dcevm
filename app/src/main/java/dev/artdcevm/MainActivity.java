package dev.artdcevm;
import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;
import dev.artdcevm.testing.ConformanceSuite;
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
        text.setText("正在测试 Android 13 ART 热替换…");
        new Thread(() -> {
            String result = new ConformanceSuite(getApplicationContext()).run().toString();
            runOnUiThread(() -> text.setText(result));
        }, "dcevm-conformance").start();
    }
}
