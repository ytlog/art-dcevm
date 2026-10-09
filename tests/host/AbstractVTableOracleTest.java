import com.github.ytlog.artdcevm.internal.AbstractVTableOracle;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public final class AbstractVTableOracleTest {
    public static void main(String[] args) throws Exception {
        Object[] proof = AbstractVTableOracle.create();
        if (proof.length != 6 || !Modifier.isAbstract(((Class<?>) proof[0]).getModifiers()) ||
            !Modifier.isAbstract(((Class<?>) proof[1]).getModifiers())) throw new AssertionError("Actual Class roles");
        Method missing=(Method)proof[3], complete=(Method)proof[4], concrete=(Method)proof[5];
        if(Modifier.isAbstract(((Class<?>)proof[2]).getModifiers()) ||
           (int)concrete.invoke(new AbstractVTableOracle.Concrete())!=41)throw new AssertionError("Actual embedded dispatch oracle");
        if (!Modifier.isAbstract(missing.getModifiers()) || Modifier.isAbstract(complete.getModifiers()) ||
            (int)complete.invoke(new AbstractVTableOracle.Concrete()) != 41)
            throw new AssertionError("Original complete/abstract Method roles");
        System.out.println("Public abstract vtable oracle: actual Class/Method reflection and original virtual+reflective invocation passed");
    }
}
