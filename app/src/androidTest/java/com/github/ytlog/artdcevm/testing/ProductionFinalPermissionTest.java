package com.github.ytlog.artdcevm.testing;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
/** Shared production scenarios; no isolated SourceEngine proof. */
public final class ProductionFinalPermissionTest {
 private void run(String name) throws Throwable {
  new ProductionPermissionScenarios(InstrumentationRegistry.getInstrumentation(),InstrumentationRegistry.getArguments()).run(name);
 }
 @Test public void finalFieldsRequired() throws Throwable {run("final-fields");}
 @Test public void fieldAccessRequired() throws Throwable {run("field-access");}
}
