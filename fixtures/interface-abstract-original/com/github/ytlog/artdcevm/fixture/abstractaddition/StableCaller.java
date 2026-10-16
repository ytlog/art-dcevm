package com.github.ytlog.artdcevm.fixture.abstractaddition;
public class StableCaller { public static int call(Contract subject) { return subject.oldValue(); } public static int warm(Contract subject) { int result=0; for(int i=0;i<10000;i++) result=subject.oldValue(); return result; } }
