package com.github.ytlog.artdcevm.testing;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import com.github.ytlog.artdcevm.fixture.CopiedChild;
import com.github.ytlog.artdcevm.fixture.CopiedDefault;
import com.github.ytlog.artdcevm.fixture.CopiedParent;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.zip.Adler32;

/** Repeated failures and successful transactions in one process, with live instances. */
public final class InterfaceAbortInstrumentation extends Instrumentation {
    private static native int armFailure(int stage);
    private static native int discardedClasses();
    private static native int failureHits();
    private static native int configureCheckJni(boolean enabled);
    private Bundle arguments;
    private int checks;

    // Debuggable applications may start with CheckJNI even when debug.checkjni
    // is zero. Switch before the engine captures and guards the JNI table.
    static void configureRuntime(Bundle arguments) {
        if (arguments == null || !arguments.containsKey("checkjni")) return;
        System.loadLibrary("artdcevm");
        boolean enabled = Boolean.parseBoolean(arguments.getString("checkjni"));
        int code = configureCheckJni(enabled);
        if (code != 0) throw new AssertionError("Cannot select JNI checking mode: " + code);
    }

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); this.arguments = arguments; start(); }
    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            configureRuntime(arguments);
            DcevmRuntime.initialize(getTargetContext());
            StandardJvmtiClient.attach(getTargetContext());
            CopiedChild child = new CopiedChild();
            CopiedDefault throughInterface = child;
            byte[] patch = asset("interface-default-body", CopiedDefault.class);
            byte[] parent = asset("copied-default-add", CopiedParent.class);
            byte[] invalidInvoke = asset("interface-invalid-invoke", CopiedDefault.class);
            // Repeat in the same process: successful redefinitions make each
            // following failed attempt own a different DexFile and obsolete chain.
            for (int cycle = 0; cycle < 3; ++cycle) {
                for (int stage = 0; stage <= 5; ++stage) {
                    int before = throughInterface.copiedValue();
                    int parentBefore = child.parentValue();
                    boolean verifierFailure = stage == 0 || stage == 5;
                    expect("arm", 0, armFailure(verifierFailure ? 0 : stage));
                    int code = redefine(CopiedDefault.class,
                            stage == 0 ? invalidReturn(patch) : stage == 5 ? invalidInvoke : patch);
                    expect("failure stage " + stage, verifierFailure ? 62 : stage == 1 ? 113 : stage == 4 ? 113 : 110, code);
                    expect("injection hit", verifierFailure ? 0 : 1, failureHits());
                    // Interface and implementation must both be discarded,
                    // including when AllocateNewClassObject returned nullptr.
                    if (discardedClasses() < 2) throw new AssertionError("cleanup did not cover both private classes");
                    ++checks;
                    expect("old interface", before, throughInterface.copiedValue());
                    expect("old virtual", before, child.copiedValue());
                    expect("new child", before, new CopiedChild().copiedValue());
                    expect("old parent", parentBefore, child.parentValue());
                    if (cycle == 1 || stage == 5) { System.gc(); System.runFinalization(); System.gc(); }
                    // Exercise ART's heap scan of the original implementation
                    // parent BEFORE a GC can hide the dangling resolved class.
                    expect("parent redefine after failure", 0, redefine(CopiedParent.class, parent));
                    expect("success after failure", 0, redefine(CopiedDefault.class, patch));
                    expect("old interface after commit", 92, throughInterface.copiedValue());
                    expect("old virtual after commit", 92, child.copiedValue());
                    expect("new child after commit", 92, new CopiedChild().copiedValue());
                    System.gc(); System.runFinalization(); System.gc();
                    expect("old interface after GC", 92, throughInterface.copiedValue());
                    expect("redefine after GC", 0, redefine(CopiedDefault.class, patch));
                    expect("new child after GC", 92, new CopiedChild().copiedValue());
                }
            }
            // Exceptions injected at the exact callback/hook boundary must
            // return through ART normally, including before cleanup roots exist.
            for (int stage : new int[]{8, 11, 12}) {
                expect("arm hook exception", 0, armFailure(stage));
                expect("hook exception " + stage, stage == 12 ? 113 : 110,
                        redefine(CopiedDefault.class, patch));
                expect("hook exception hit", 1, failureHits());
                expect("old dispatch after hook exception", 92, throughInterface.copiedValue());
                expect("parent after hook exception", 0, redefine(CopiedParent.class, parent));
                expect("interface after hook exception", 0, redefine(CopiedDefault.class, patch));
                System.gc(); System.runFinalization(); System.gc();
                expect("new dispatch after hook exception and GC", 92, new CopiedChild().copiedValue());
            }
            // The submitted bytes do not reference LateCopiedChild. A real
            // ClassFileLoadHook introduces it only after the loaded-class
            // snapshot; verification prepares it and the engine must retry.
            String late = "Lcom/github/ytlog/artdcevm/fixture/LateCopiedChild;";
            expect("late child initially unloaded", 0, StandardJvmtiClient.isLoaded(late) ? 1 : 0);
            long retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            long transformations = StandardJvmtiClient.classFileReplacements();
            expect("reset injection", 0, armFailure(0));
            expect("install transforming hook", 0, StandardJvmtiClient.setClassFileReplacement(
                    "com/github/ytlog/artdcevm/fixture/CopiedDefault", asset("interface-retry", CopiedDefault.class)));
            try {
                expect("interface retry", 0, redefine(CopiedDefault.class, patch));
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            expect("actual retry", 1, (int) (DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries));
            expect("hook rerun", 2, (int) (StandardJvmtiClient.classFileReplacements() - transformations));
            if (discardedClasses() < 2) throw new AssertionError("retry did not discard its private classes");
            ++checks;
            expect("late child now loaded", 1, StandardJvmtiClient.isLoaded(late) ? 1 : 0);
            CopiedDefault lateChild = (CopiedDefault) Class.forName("com.github.ytlog.artdcevm.fixture.LateCopiedChild")
                    .getConstructor().newInstance();
            expect("late child dispatch", 92, lateChild.copiedValue());
            expect("parent scan after retry", 0, redefine(CopiedParent.class, parent));
            System.gc(); System.runFinalization(); System.gc();
            expect("late child after GC", 92, lateChild.copiedValue());
            expect("interface after retry and GC", 0, redefine(CopiedDefault.class, patch));
            expect("old child after retry", 92, child.copiedValue());
            // A second implementation implements the target directly, without
            // CopiedParent. It is introduced after the Native subtype snapshot.
            String direct = "com.github.ytlog.artdcevm.fixture.LateSnapshotImplementation";
            String directDescriptor = "L" + direct.replace('.', '/') + ";";
            expect("direct implementation initially unloaded", 0, StandardJvmtiClient.isLoaded(directDescriptor) ? 1 : 0);
            retries = DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1);
            transformations = StandardJvmtiClient.classFileReplacements();
            expect("install direct implementation hook", 0, StandardJvmtiClient.setClassFileReplacement(
                    "com/github/ytlog/artdcevm/fixture/CopiedDefault", asset("interface-snapshot-retry", CopiedDefault.class)));
            try {
                expect("direct implementation retry", 0, redefine(CopiedDefault.class, patch));
            } finally {
                StandardJvmtiClient.setClassFileReplacement(null, null);
            }
            expect("direct implementation actual retry", 1,
                    (int) (DcevmRuntime.capabilities().optLong("subclassLoadRetries", -1) - retries));
            expect("direct implementation hook rerun", 2,
                    (int) (StandardJvmtiClient.classFileReplacements() - transformations));
            expect("direct implementation now loaded", 1, StandardJvmtiClient.isLoaded(directDescriptor) ? 1 : 0);
            CopiedDefault directChild = (CopiedDefault) Class.forName(direct).getConstructor().newInstance();
            expect("direct implementation after retry", 94, directChild.copiedValue());
            expect("restore after direct implementation retry", 0, redefine(CopiedDefault.class, patch));
            System.gc(); System.runFinalization(); System.gc();
            expect("direct implementation after GC", 93, directChild.copiedValue());
            // Proxy rejection is decided by the public Native snapshot path.
            Object proxy = java.lang.reflect.Proxy.newProxyInstance(CopiedDefault.class.getClassLoader(),
                    new Class<?>[]{CopiedDefault.class}, (object, method, values) -> 92);
            expect("Proxy snapshot rejection", 79, redefine(CopiedDefault.class, patch));
            expect("Proxy remains usable", 92, ((CopiedDefault) proxy).copiedValue());
            expect("old child after Proxy rejection", 92, child.copiedValue());
            result.putInt("checks", checks);
            result.putBoolean("completed", true);
            finish(Activity.RESULT_OK, result);
        } catch (Throwable error) {
            result.putString("error", error.toString());
            result.putInt("checks", checks);
            finish(Activity.RESULT_CANCELED, result);
        }
    }
    private void expect(String name, int expected, int actual) {
        if (actual != expected) throw new AssertionError(name + ": expected " + expected + ", got " + actual);
        ++checks;
    }
    private int redefine(Class<?> klass, byte[] dex) { return StandardJvmtiClient.redefine(new Class<?>[]{klass}, new byte[][]{dex}); }
    private byte[] asset(String variant, Class<?> klass) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open(variant + "/" + klass.getName().replace('.', '/') + ".dex");
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            return output.toByteArray();
        }
    }
    private static int u32(byte[] bytes, int at) {
        return (bytes[at] & 255) | (bytes[at + 1] & 255) << 8 | (bytes[at + 2] & 255) << 16 | (bytes[at + 3] & 255) << 24;
    }
    private static int uleb(byte[] bytes, int[] cursor) {
        int value = 0, shift = 0, next;
        do { next = bytes[cursor[0]++] & 255; value |= (next & 127) << shift; shift += 7; } while ((next & 128) != 0);
        return value;
    }
    private static byte[] invalidReturn(byte[] original) throws Exception {
        byte[] bytes = original.clone();
        int[] cursor = {u32(bytes, u32(bytes, 100) + 24)};
        int[] counts = new int[4];
        for (int i = 0; i < 4; ++i) counts[i] = uleb(bytes, cursor);
        for (int i = 0; i < counts[0] + counts[1]; ++i) { uleb(bytes, cursor); uleb(bytes, cursor); }
        if (counts[2] + counts[3] != 1) throw new AssertionError("Expected one interface default method");
        uleb(bytes, cursor); uleb(bytes, cursor);
        int code = uleb(bytes, cursor);
        int instruction = code + 16 + u32(bytes, code + 12) * 2 - 2;
        if (code == 0 || bytes[instruction] != 0x0f) throw new AssertionError("Expected return-int");
        bytes[instruction] = 0x11; // return-object in an int method: real ART verifier failure.
        System.arraycopy(MessageDigest.getInstance("SHA-1").digest(java.util.Arrays.copyOfRange(bytes, 32, bytes.length)), 0, bytes, 12, 20);
        Adler32 checksum = new Adler32(); checksum.update(bytes, 12, bytes.length - 12);
        int sum = (int) checksum.getValue();
        for (int i = 0; i < 4; ++i) bytes[8 + i] = (byte) (sum >>> (i * 8));
        return bytes;
    }
}
