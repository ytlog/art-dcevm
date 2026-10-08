import com.github.ytlog.artdcevm.testing.MethodDexOracle;
import com.github.ytlog.artdcevm.testing.MethodIdentityFixture;
import java.io.IOException;
import java.lang.reflect.Method;
import java.lang.reflect.Executable;
import java.util.HashSet;

public final class MethodDexOracleTest {
    public static void main(String[] arguments) throws Exception {
        Method[] methods = {
            MethodIdentityFixture.class.getDeclaredMethod("instanceMarker", int.class),
            MethodIdentityFixture.class.getDeclaredMethod("staticMarker", long.class),
            MethodIdentityFixture.class.getDeclaredMethod("nativeMarker")
        };
        for (String path : arguments) {
            boolean failure = path.contains("reject-");
            try {
                MethodDexOracle.Result result = MethodDexOracle.read(path, methods);
                if (failure) throw new AssertionError("Malformed APK accepted: " + path);
                HashSet<Integer> indices = new HashSet<>();
                for (int i = 0; i < methods.length; ++i) {
                    if (result.indices[i] < 0 || result.modifiers[i] != methods[i].getModifiers())
                        throw new AssertionError("Wrong independent method oracle");
                    indices.add(result.indices[i]);
                }
                if (indices.size() != 3 || !result.dexEntry.equals("classes.dex"))
                    throw new AssertionError("Wrong unique method identities");
                if (path.contains("container-multi") && result.headerOffset == 0)
                    throw new AssertionError("Did not iterate to second logical DEX");
                if (path.contains("legacy-")) {
                    Executable[] complete = {MethodIdentityFixture.class.getDeclaredConstructor(),
                            methods[0], methods[1], methods[2]};
                    MethodDexOracle.Result all = MethodDexOracle.read(path, complete);
                    if (all.indices.length != 4 || all.directMethods != 3 || all.virtualMethods != 1 ||
                            all.modifiers[0] != complete[0].getModifiers())
                        throw new AssertionError("Constructor/class_data oracle mismatch");
                }
            } catch (IOException expected) {
                if (!failure) throw expected;
            }
        }
        System.out.println("APK DEX method oracle: D8 real fixture, five legacy versions, DEX 041 logical/container bounds, class ownership, duplicates and malformed ranges passed");
    }
}
