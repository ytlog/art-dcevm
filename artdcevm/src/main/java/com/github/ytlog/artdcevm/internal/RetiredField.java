package com.github.ytlog.artdcevm.internal;

/**
 * Targets for reflective {@code Field} objects whose instance field was retired
 * by a structural redefinition. The engine remaps such objects here, one field
 * per primitive kind, so a cached {@code Field} never addresses an object of the
 * redefined class: its receiver check fails with {@code IllegalArgumentException}.
 */
public final class RetiredField {
    private boolean retiredBoolean;
    private byte retiredByte;
    private char retiredChar;
    private short retiredShort;
    private int retiredInt;
    private long retiredLong;
    private float retiredFloat;
    private double retiredDouble;
    private Object retiredReference;

    private RetiredField() {}
}
