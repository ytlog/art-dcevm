import com.github.ytlog.artdcevm.internal.JniExceptionUpcallFixture;
public final class JniExceptionCalibrationHost {
    private static native boolean calibrate();
    private static native boolean blockedWindow();
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        if (!blockedWindow()) throw new AssertionError("schema Run was not rejected");
        if (!calibrate() || !calibrate()) throw new AssertionError("closed original JNI calibration failed");
        // Successful calibration retires its temporary registration. Public run
        // cannot create a new receipt outside the sealed Service invocation.
        try {
            JniExceptionUpcallFixture.run(new AssertionError("outside invocation"));
            throw new AssertionError("native callbacks leaked after calibration");
        } catch (UnsatisfiedLinkError expected) {}
        Thread other = new Thread(() -> {
            if (!calibrate()) throw new AssertionError("same VM readonly receipt missing");
        });
        other.setUncaughtExceptionHandler((thread, error) -> { error.printStackTrace(); System.exit(1); });
        other.start(); other.join();
        System.out.println("Real JVM CheckJNI: closed Service original catch receipt, VM binding, schema-window rejection and registration retirement passed");
    }
}
