package com.github.ytlog.artdcevm.internal;

/** Ordinary DEX instructions provide independent resolved-cache witnesses. */
public class SourceCacheFixture {
    public volatile int number = 37;

    public int read() {
        return number;
    }

    // GENERATED_CACHE_PAIR_ID_PADDING

    public static Class<?> warm(SourceCacheFixture fixture) {
        SourceCacheFixture fresh = new SourceCacheFixture();
        // The generated private static field has immutable DEX index 1;
        // number has index 1025. Reading it causes an actual same-slot collision
        // before the following genuine iget. No native cache entries are edited.
        if (cachePadding0001 != null) {
            throw new IllegalStateException("Source cache collision field changed");
        }
        Class<?> expected = SourceCacheFixture.class;
        if (fixture == null || fixture.getClass() != expected ||
                fixture.read() + fixture.number != 74 || fresh.getClass() != expected ||
                fresh.read() + fresh.number != 74) {
            throw new IllegalStateException("Source cache fixture behavior changed");
        }
        return expected;
    }
}
