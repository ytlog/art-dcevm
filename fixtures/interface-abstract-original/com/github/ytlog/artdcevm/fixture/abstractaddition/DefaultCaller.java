package com.github.ytlog.artdcevm.fixture.abstractaddition;
public class DefaultCaller { public static String call(DistantDefault subject) { return subject.required(); } public static String warm(DistantDefault subject) { String result = ""; for(int i=0;i<10000;i++) result = subject.required(); return result; } }
