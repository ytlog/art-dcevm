package com.github.ytlog.artdcevm.internal;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/** Isolated real-frame proof; never rewrites application definitions. */
public final class PrivateContinuationProbe {
    private PrivateContinuationProbe() {}
    private static native int checkpoint(Class<?> type, byte[] dex);
    static void change(Class<?> type, String variant) throws Exception {
        String resource = "/" + variant + "/" + type.getName().replace('.', '/') + ".dex";
        try (InputStream input = PrivateContinuationProbe.class.getResourceAsStream(resource)) {
            if (input == null) throw new AssertionError("missing continuation proof DEX");
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] block = new byte[4096];
            int count;
            while ((count = input.read(block)) != -1) output.write(block, 0, count);
            if (checkpoint(type, output.toByteArray()) != 0) throw new AssertionError("continuation proof transaction");
        }
    }
    public static class Subject {
        private int direct() throws Exception {
            int saved = 19;
            Object marker = new Object();
            int[] box = {1};
            change(Subject.class, "continuation-next");
            synchronized (marker) {
                long w = wide(0x123456780000L);
                double d = real(0.5);
                Object reference = object(marker);
                System.gc();
                effect(box);
                int r = range(1L, 2L, 3L);
                int caught = 0;
                try { fail(); } catch (ArithmeticException expected) { caught++; }
                finally { saved += 2; }
                try { Subject receiver = null; receiver.number(); }
                catch (NullPointerException expected) { caught++; }
                if (w != 0x12345678000dL || d != 13.5 || reference != marker || box[0] != 14 || r != 19 || caught != 2)
                    throw new AssertionError("private direct continuation behavior");
                return number() + saved - 2;
            }
        }
        private int number() { return 13; }
        private long wide(long value) { return value + 13; }
        private double real(double value) { return value + 13; }
        private Object object(Object value) { return value; }
        private void effect(int[] value) { value[0] += 13; }
        private int range(long a, long b, long c) { return (int)(a + b + c) + 13; }
        private int fail() { throw new ArithmeticException(); }
        public int await(Object marker) throws Exception {
            change(Subject.class, "continuation-private");
            int n = number();
            long w = wide(0x123456780000L);
            double d = real(0.5);
            Object reference = object(marker);
            System.gc();
            int[] box = {1}; effect(box);
            int r = range(1L, 2L, 3L);
            int resumed = direct();
            // The nested commit changed number() again. An older caller must
            // select that newest body after completing its in-flight call.
            int after = number();
            int caught = 0;
            try { fail(); } catch (ArithmeticException expected) { caught++; }
            try { Subject receiver = null; receiver.number(); } catch (NullPointerException expected) { caught++; }
            return n == 13 && w == 0x12345678000dL && d == 13.5 && reference == marker && box[0] == 14 && r == 19 && resumed == 36 && after == 17 && caught == 2 ? 1 : 0;
        }
    }
    public interface Contract {
        private int direct() throws Exception {
            int saved = 19;
            Object marker = new Object();
            int[] box = {1};
            change(Contract.class, "continuation-next");
            synchronized (marker) {
                long w = wide(0x123456780000L);
                double d = real(0.5);
                Object reference = object(marker);
                System.gc();
                effect(box);
                int r = range(1L, 2L, 3L);
                int caught = 0;
                try { fail(); } catch (ArithmeticException expected) { caught++; }
                finally { saved += 2; }
                try { Contract receiver = null; receiver.number(); }
                catch (NullPointerException expected) { caught++; }
                if (w != 0x12345678000dL || d != 13.5 || reference != marker || box[0] != 14 || r != 19 || caught != 2)
                    throw new AssertionError("private direct continuation behavior");
                return number() + saved - 2;
            }
        }
        private int number() { return 13; }
        private long wide(long value) { return value + 13; }
        private double real(double value) { return value + 13; }
        private Object object(Object value) { return value; }
        private void effect(int[] value) { value[0] += 13; }
        private int range(long a, long b, long c) { return (int)(a + b + c) + 13; }
        private int fail() { throw new ArithmeticException(); }
        default int await(Object marker) throws Exception {
            change(Contract.class, "continuation-private");
            int n = number();
            long w = wide(0x123456780000L);
            double d = real(0.5);
            Object reference = object(marker);
            System.gc();
            int[] box = {1}; effect(box);
            int r = range(1L, 2L, 3L);
            int resumed = direct();
            // The nested commit changed number() again. An older caller must
            // select that newest body after completing its in-flight call.
            int after = number();
            int caught = 0;
            try { fail(); } catch (ArithmeticException expected) { caught++; }
            try { Contract receiver = null; receiver.number(); } catch (NullPointerException expected) { caught++; }
            return n == 13 && w == 0x12345678000dL && d == 13.5 && reference == marker && box[0] == 14 && r == 19 && resumed == 36 && after == 17 && caught == 2 ? 1 : 0;
        }
    }
    public static final class Impl implements Contract {}
    public static void run() throws Exception {
        Object marker = new Object();
        if (new Subject().await(marker) != 1 || new Impl().await(marker) != 1) throw new AssertionError("private continuation behavior");
    }
}
