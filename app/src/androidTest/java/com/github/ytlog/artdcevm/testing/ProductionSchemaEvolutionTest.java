package com.github.ytlog.artdcevm.testing;

import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;

/** JUnit and main-APK instrumentation share the exact production scenarios. */
public final class ProductionSchemaEvolutionTest {
    private void run(String name) throws Throwable {
        new ProductionSchemaScenarios(InstrumentationRegistry.getInstrumentation(),
                InstrumentationRegistry.getArguments()).run(name);
    }
    @Test public void fieldTypeEvolutionRequired() throws Throwable {run("field-types");}
    @Test public void fieldStorageEvolutionRequired() throws Throwable {run("field-staticness");}
    @Test public void methodSignatureEvolutionRequired() throws Throwable {run("method-signature");}
    @Test public void nativeJavaTransitionsRequired() throws Throwable {run("native-java");}
    @Test public void hierarchyAdditionRequired() throws Throwable {run("hierarchy-addition");}
    @Test public void hierarchyNarrowingRequired() throws Throwable {run("hierarchy-narrowing");}
}
