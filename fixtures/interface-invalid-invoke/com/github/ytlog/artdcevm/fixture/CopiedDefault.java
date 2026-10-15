package com.github.ytlog.artdcevm.fixture;

public interface CopiedDefault {
    // The fixture builder replaces this String receiver with an int while
    // retaining invoke-virtual String.length(), so ART must reject the DEX.
    default int copiedValue() { return "incompatible-receiver".length(); }
}
