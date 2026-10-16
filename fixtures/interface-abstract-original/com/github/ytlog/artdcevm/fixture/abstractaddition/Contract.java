package com.github.ytlog.artdcevm.fixture.abstractaddition;

public interface Contract { default int oldValue() { return 11; } String required(); }
