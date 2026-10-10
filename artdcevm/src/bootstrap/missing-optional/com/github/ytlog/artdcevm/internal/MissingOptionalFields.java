package com.github.ytlog.artdcevm.internal;

/** A runtime declaration whose optional reference type is intentionally absent. */
public final class MissingOptionalFields {
    public int number;
    public MissingOptionalType optional;
}
// Compile-only stub: build-production-bootstrap removes this class before D8.
final class MissingOptionalType {}
