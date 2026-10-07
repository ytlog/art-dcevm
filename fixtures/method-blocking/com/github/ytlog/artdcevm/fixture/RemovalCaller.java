package com.github.ytlog.artdcevm.fixture;
public final class RemovalCaller {
    public static int hot(Evolving receiver) { return receiver.addedMethod(); }
    public static int cold(Evolving receiver) { return receiver.addedMethod(); }
}
