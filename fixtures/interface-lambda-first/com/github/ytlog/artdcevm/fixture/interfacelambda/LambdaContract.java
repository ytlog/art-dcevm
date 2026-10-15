package com.github.ytlog.artdcevm.fixture.interfacelambda;
import java.util.function.IntSupplier;
// The recipe clears javac's synthetic bit and uses Kotlin's helper names,
// preserving private static implementations before D8. D8 itself emits
// the public synthetic bridges and the per-class lambda DEX files.
public interface LambdaContract {
 default int seed() { return 5; }
 default int value() { LambdaContract self = this; IntSupplier first = () -> self.seed(); return first.getAsInt(); }
}
