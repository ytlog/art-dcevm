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
        Class<?> expected = SourceCacheFixture.class;
        if (fixture == null || fixture.getClass() != expected ||
                fixture.read() + fixture.number != 74 || fresh.getClass() != expected ||
                fresh.read() + fresh.number != 74) {
            throw new IllegalStateException("Source cache fixture behavior changed");
        }
        return expected;
    }
}
