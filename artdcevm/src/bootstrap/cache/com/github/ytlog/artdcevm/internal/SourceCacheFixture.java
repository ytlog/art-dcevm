package com.github.ytlog.artdcevm.internal;

/** Independent original ART cache writers, separate from business fixtures. */
public class SourceCacheFixture {
    public static volatile int number = 37;
    public static int read() { return number; }

    // GENERATED_CACHE_PAIR_ID_PADDING

    public static Class<?> warm(SourceCacheFixture fixture) {
        return ZCacheWriter.warm(fixture);
    }
}

/** Different caller Class, same immutable DEX and defining loader. */
final class ZCacheWriter {
    static Class<?> warm(SourceCacheFixture fixture) {
        SourceCacheFixture fresh = new SourceCacheFixture();
        // Cross-class static references cannot take the same-class or receiver
        // FindFieldFast shortcut. The real field ID 1 displaces slot 1 before
        // the real field ID 1025 is resolved again by ART's original writer.
        if (SourceCacheFixture.cachePadding0001 != null) {
            throw new IllegalStateException("Source cache collision field changed");
        }
        int number = SourceCacheFixture.number;
        Class<?> expected = SourceCacheFixture.class;
        int padding = SourceCacheFixture.cachePaddingMethod0001();
        int read = SourceCacheFixture.read();
        if (fixture == null || fixture.getClass() != expected ||
                number + read != 74 || padding != 1 || fresh.getClass() != expected) {
            throw new IllegalStateException("Source cache fixture behavior changed");
        }
        return expected;
    }
}
