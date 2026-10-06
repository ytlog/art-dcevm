package dev.artdcevm.api;

import org.json.JSONObject;

/** Public engine contract; no ART offsets, hooks, or Android release details. */
public interface RuntimeBackend {
    String id();
    JSONObject capabilities() throws Exception;
    PatchResult apply(PatchRequest request);
    JSONObject analyze(PatchRequest request) throws Exception;
    PatchResult enableValidationHooks();
    /** Initialize the packaged evolution module and verify the device-specific ABI. */
    PatchResult enableEvolution();
}
