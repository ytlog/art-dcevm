import com.github.ytlog.artdcevm.internal.VarHandleClassOracle;
import java.lang.invoke.VarHandle;

public final class VarHandleClassOracleTest {
    public static void main(String[] args) {
        VarHandle handle = VarHandleClassOracle.create();
        int[] values = {7, 11};
        if ((int) handle.get(values, 1) != 11) throw new AssertionError("Original getter");
        handle.set(values, 0, 41);
        if ((int) handle.get(values, 0) != 41 || values[0] != 41 || values[1] != 11)
            throw new AssertionError("Original setter/result preservation");
        if (handle.coordinateTypes().size() != 2) throw new AssertionError("Array coordinates");
        System.out.println("Public non-field array oracle: actual production Java original read/write passed");
    }
}
