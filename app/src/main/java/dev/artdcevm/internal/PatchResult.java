package dev.artdcevm.internal;

public final class PatchResult {
    public final int code;
    public final String detail;
    public PatchResult(int code, String detail) { this.code = code; this.detail = detail; }
    public boolean installed() { return code == 0; }
    @Override public String toString() { return code + ": " + detail; }
}
