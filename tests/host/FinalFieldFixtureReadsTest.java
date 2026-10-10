import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Paths;

public final class FinalFieldFixtureReadsTest {
    public static void main(String[] args) throws Exception {
        try (URLClassLoader loader = new URLClassLoader(new URL[] {
                Paths.get(args[0]).toUri().toURL() }, null)) {
            Class<?> type = Class.forName(
                    "com.github.ytlog.artdcevm.fixture.FinalFieldSubject", true, loader);
            Object object = type.getDeclaredConstructor().newInstance();
            Field instance = type.getField("value"), shared = type.getField("staticValue");
            // Host-only fixture check: seed the state that production migration
            // preserves. No ART layout, address, permission or DCEVM is inferred.
            Class<?> unsafeType = Class.forName("sun.misc.Unsafe");
            Field singleton = unsafeType.getDeclaredField("theUnsafe");
            singleton.setAccessible(true);
            Object unsafe = singleton.get(null);
            Method objectOffset = unsafeType.getMethod("objectFieldOffset", Field.class);
            Method staticOffset = unsafeType.getMethod("staticFieldOffset", Field.class);
            Method staticBase = unsafeType.getMethod("staticFieldBase", Field.class);
            Method putInt = unsafeType.getMethod("putInt", Object.class, long.class, int.class);
            putInt.invoke(unsafe, object, objectOffset.invoke(unsafe, instance), 73);
            putInt.invoke(unsafe, staticBase.invoke(unsafe, shared), staticOffset.invoke(unsafe, shared), 84);
            if (instance.getInt(object) != 73 || shared.getInt(null) != 84
                    || ((Integer) type.getMethod("read").invoke(object)) != 73
                    || ((Integer) type.getMethod("readStatic").invoke(null)) != 84)
                throw new AssertionError("Final fixture folded a read instead of consuming preserved state");
            System.out.println("Blank final fixture: real reads consume preserved instance=73/static=84");
        }
    }
}
