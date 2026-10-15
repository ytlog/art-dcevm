package com.github.ytlog.artdcevm.fixture.interfacelambda;
import java.util.function.IntSupplier;
public interface LambdaContract {
 default int seed() { return 5; }
 default int value() { return seed(); }
}
