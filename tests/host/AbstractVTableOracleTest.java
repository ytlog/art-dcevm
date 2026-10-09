import com.github.ytlog.artdcevm.internal.AbstractVTableOracle;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public final class AbstractVTableOracleTest {
    public static void main(String[] args) throws Exception {
        Object[] proof = AbstractVTableOracle.create();
        if (proof.length != 4 || !Modifier.isAbstract(((Class<?>) proof[0]).getModifiers()) ||
            !Modifier.isAbstract(((Class<?>) proof[1]).getModifiers())) throw new AssertionError("Actual Class roles");
        Method missing=(Method)proof[2], complete=(Method)proof[3];
        if (!Modifier.isAbstract(missing.getModifiers()) || Modifier.isAbstract(complete.getModifiers()) ||
            (int)complete.invoke(new AbstractVTableOracle.Concrete()) != 41)
            throw new AssertionError("Original complete/abstract Method roles");
        System.out.println("Public abstract vtable oracle: actual Class/Method reflection and original virtual+reflective invocation passed");
    }
}
