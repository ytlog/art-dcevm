import com.github.ytlog.artdcevm.internal.InitializerStateOracle;

/** Executes the unchanged production failure body and Java exception oracle. */
public final class InitializerFailureOracleTest {
    public static void main(String[] args) throws Exception {
        Class<?> fixture = InitializerStateOracle.failedClass();
        if (Class.forName(fixture.getName(), false, fixture.getClassLoader()) != fixture) {
            throw new AssertionError("Resolved Class identity changed");
        }
        if (InitializerStateOracle.runFailure() != 1) {
            throw new AssertionError("Original EIIE/NCDFE/body oracle failed");
        }
        try {
            Class.forName(fixture.getName(), true, fixture.getClassLoader());
            throw new AssertionError("Erroneous Class unexpectedly initialized");
        } catch (NoClassDefFoundError expected) {
            // The unchanged Java oracle already checked the first EIIE cause
            // and the next NCDFE without executing the failing body again.
        }
        System.out.println("Original Java resolved-Class clinit: EIIE then NCDFE, single body PASS");
    }
}
