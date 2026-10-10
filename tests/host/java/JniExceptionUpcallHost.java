import com.github.ytlog.artdcevm.internal.JniExceptionUpcallFixture;
public final class JniExceptionUpcallHost {
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        Throwable token = new IllegalStateException("identity token");
        for (int i = 0; i < 100; i++) {
            if (!JniExceptionUpcallFixture.run(token)) throw new AssertionError("pending identity failed");
        }
        Thread independent = new Thread(() -> {
            if (!JniExceptionUpcallFixture.run(new AssertionError("other thread"))) {
                throw new AssertionError("independent thread failed");
            }
        });
        independent.setUncaughtExceptionHandler((thread, error) -> { error.printStackTrace(); System.exit(1); });
        independent.start(); independent.join();
        System.out.println("Real JVM CheckJNI: original pending exception returned to Java; catch identity and one-shot receipt passed");
    }
}
