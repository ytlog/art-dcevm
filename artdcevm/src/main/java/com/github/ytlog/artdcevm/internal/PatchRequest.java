package com.github.ytlog.artdcevm.internal;

public final class PatchRequest {
    public enum Operation {
        REDEFINE_BODY(0), REDEFINE_STRUCTURE(1), EVOLVE_SCHEMA(2);
        public final int wireCode;
        Operation(int wireCode) { this.wireCode = wireCode; }
    }
    public final Operation operation;
    private final Class<?>[] targets;
    private final byte[][] dex;

    public PatchRequest(Operation operation, Class<?>[] targets, byte[][] dex) {
        if (operation == null || targets == null || dex == null || targets.length == 0 || targets.length != dex.length)
            throw new IllegalArgumentException("A nonempty class/dex batch is required");
        this.operation = operation;
        this.targets = targets.clone();
        this.dex = new byte[dex.length][];
        for (int i = 0; i < dex.length; i++) {
            if (targets[i] == null || dex[i] == null || dex[i].length == 0) throw new IllegalArgumentException("Null/empty definition");
            this.dex[i] = dex[i].clone();
        }
    }
    public Class<?>[] targets() { return targets.clone(); }
    public byte[][] dex() {
        byte[][] copy = new byte[dex.length][];
        for (int i = 0; i < dex.length; i++) copy[i] = dex[i].clone();
        return copy;
    }
}
