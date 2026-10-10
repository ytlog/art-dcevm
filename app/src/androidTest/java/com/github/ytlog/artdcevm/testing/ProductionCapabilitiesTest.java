package com.github.ytlog.artdcevm.testing;

import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;

/** JUnit adapter for the identical application-only production suite. */
public final class ProductionCapabilitiesTest {
    @Test public void productionBootstrapAndFormalEvolution() throws Throwable {
        new ProductionCapabilitiesScenarios(InstrumentationRegistry.getInstrumentation(),
            InstrumentationRegistry.getArguments()).run();
    }
}
