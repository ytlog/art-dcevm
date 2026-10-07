package com.github.ytlog.artdcevm.internal;

import org.json.JSONObject;

/** Public engine contract; no ART offsets, hooks, or Android release details. */
public interface RuntimeBackend {
    String id();
    JSONObject capabilities() throws Exception;
    PatchResult apply(PatchRequest request);
    JSONObject analyze(PatchRequest request) throws Exception;
    PatchResult enableValidationHooks();
    /** Route standard JVMTI redefinition through available device extensions. */
    default PatchResult enableStandardRedefineRouting() { return new PatchResult(0, "OK"); }
    /** Initialize the packaged evolution module and verify the device-specific ABI. */
    PatchResult enableEvolution();
}
