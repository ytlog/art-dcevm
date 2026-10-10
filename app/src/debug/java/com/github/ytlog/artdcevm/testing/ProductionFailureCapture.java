package com.github.ytlog.artdcevm.testing;

import org.json.JSONObject;

/** Diagnostics run after JNI has returned; they never replace the original failure. */
final class ProductionFailureCapture {
    interface Probe { String capabilities(); String errorName(int code); }
    static void record(JSONObject report, Throwable original, Probe probe) {
        try { report.put("failureType", original.getClass().getName()).put("failure", String.valueOf(original.getMessage())); }
        catch (Throwable ignored) { /* Preserve the original exception even if reporting fails. */ }
        try { report.put("capabilitiesAtFailure", new JSONObject(probe.capabilities())); }
        catch (Throwable diagnostic) { note(report, "failureCapabilitiesDiagnostic", diagnostic); }
        try { report.put("failureNativeErrorName", probe.errorName(98)); }
        catch (Throwable diagnostic) { note(report, "failureNativeErrorDiagnostic", diagnostic); }
    }
    private static void note(JSONObject report, String key, Throwable diagnostic) {
        try { report.put(key, diagnostic.toString()); } catch (Throwable ignored) { }
    }
    private ProductionFailureCapture() { }
}
