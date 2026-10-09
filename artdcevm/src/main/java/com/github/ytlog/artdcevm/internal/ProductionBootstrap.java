package com.github.ytlog.artdcevm.internal;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import dalvik.system.InMemoryDexClassLoader;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.lang.reflect.Method;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Raw installed APK bytes and an isolated library-owned additive fixture. */
public final class ProductionBootstrap {
    private static Context application;
    private ProductionBootstrap() {}
    /** Readonly public/native ID proof; null means the fresh field is unambiguous. */
    public static native String verifyLiveFieldId(java.lang.reflect.Field field);
    public static synchronized void configure(Context context) {
        application = context.getApplicationContext();
    }
    private static byte[] read(InputStream input) throws Exception {
        try (InputStream in = input; ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] chunk = new byte[16384]; int n;
            while ((n = in.read(chunk)) != -1) {
                if (bytes.size() > 64 * 1024 * 1024 - n)
                    throw new IllegalArgumentException("Bootstrap DEX exceeds supported extent");
                bytes.write(chunk, 0, n);
            }
            return bytes.toByteArray();
        }
    }
    static final class Inputs {
        final byte[][] dex;
        final Class<?> additiveClass;
        final byte[] additiveDex;
        final Object storage;
        final byte[] cacheDex;
        final Class<?> cacheClass;
        final Object cacheFixture;
        final Method cacheRead, cacheWarm;
        final java.lang.reflect.Field cacheField;
        final Object methodReceiver = new EvolutionCalibration();
        final MethodHandle[] methodHandles;
        final Method[] methods;
        final VarHandle instanceHandle, staticHandle;
        final MethodHandle instanceGetter, instanceSetter, staticGetter, staticSetter;
        Inputs(byte[][] dex, Class<?> target, byte[] replacement, byte[] cacheDex) throws Exception {
            this.dex = dex; additiveClass = target; additiveDex = replacement;
            this.cacheDex = cacheDex;
            ClassLoader cacheLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(cacheDex), null);
            cacheClass = Class.forName("com.github.ytlog.artdcevm.internal.SourceCacheFixture", true, cacheLoader);
            cacheFixture = cacheClass.getDeclaredConstructor().newInstance();
            cacheRead = cacheClass.getDeclaredMethod("read");
            cacheWarm = cacheClass.getDeclaredMethod("warm", cacheClass);
            cacheField = cacheClass.getDeclaredField("number");
            if (cacheWarm.invoke(null, cacheFixture) != cacheClass)
                throw new IllegalStateException("Isolated source cache witness changed");
            storage = new CalibrationFixture();
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            methods = new Method[] {EvolutionCalibration.class.getDeclaredMethod("compute"),
                EvolutionCalibration.class.getDeclaredMethod("privateAccessCalibration"),
                EvolutionCalibration.class.getDeclaredMethod("staticInvokeCalibration")};
            methodHandles = new MethodHandle[methods.length];
            for (int i = 0; i < methods.length; i++) {
                methods[i].setAccessible(true);
                methodHandles[i] = lookup.unreflect(methods[i]);
            }
            instanceHandle = lookup.findVarHandle(CalibrationFixture.class, "intValue", int.class);
            staticHandle = lookup.findStaticVarHandle(CalibrationFixture.class, "staticIntValue", int.class);
            instanceGetter = lookup.findGetter(CalibrationFixture.class, "intValue", int.class);
            instanceSetter = lookup.findSetter(CalibrationFixture.class, "intValue", int.class);
            staticGetter = lookup.findStaticGetter(CalibrationFixture.class, "staticIntValue", int.class);
            staticSetter = lookup.findStaticSetter(CalibrationFixture.class, "staticIntValue", int.class);
        }
    }
    static Class<?> warmSourceCache(Inputs input) throws Exception {
        return (Class<?>) input.cacheWarm.invoke(null, input.cacheFixture);
    }
    static int invokeMethod(Inputs input) throws Throwable {
        return (int) input.methodHandles[0].invoke(input.methodReceiver);
    }
    static int invokeField(Inputs input, int index) throws Throwable {
        return index == 0 ? (int) input.instanceGetter.invoke(input.storage)
                          : (int) input.staticGetter.invoke();
    }
    static boolean exerciseFieldHandles(Inputs input) throws Throwable {
        CalibrationFixture object = (CalibrationFixture) input.storage;
        int instance = object.intValue, statik = CalibrationFixture.staticIntValue;
        if ((int) input.instanceGetter.invoke(object) != instance ||
            (int) input.staticGetter.invoke() != statik) return false;
        input.instanceSetter.invoke(object, instance);
        input.staticSetter.invoke(statik);
        if ((int) input.instanceHandle.get(object) != instance ||
            (int) input.staticHandle.get() != statik) return false;
        input.instanceHandle.set(object, instance);
        input.staticHandle.set(statik);
        return object.intValue == instance && CalibrationFixture.staticIntValue == statik;
    }
    interface Checked { void run() throws Throwable; }
    private static boolean rejected(Checked action, boolean field) throws Throwable {
        try { action.run(); return false; }
        catch (NoSuchFieldError expected) { if (!field) throw expected; return true; }
        catch (NoSuchMethodError expected) { if (field) throw expected; return true; }
    }
    // These library-owned calibration members are retired permanently only
    // after their original values and the native dispatch targets were proven.
    static boolean verifyRetiredHandles(Inputs input) throws Throwable {
        Object object = input.storage;
        return rejected(() -> input.methodHandles[0].invoke(input.methodReceiver), false) &&
            rejected(() -> input.instanceGetter.invoke(object), true) &&
            rejected(() -> input.instanceSetter.invoke(object, 9), true) &&
            rejected(() -> input.staticGetter.invoke(), true) &&
            rejected(() -> input.staticSetter.invoke(9), true) &&
            rejected(() -> input.instanceHandle.get(object), true) &&
            rejected(() -> input.instanceHandle.set(object, 9), true) &&
            rejected(() -> input.staticHandle.get(), true) &&
            rejected(() -> input.staticHandle.set(9), true);
    }
    // Native owns every resulting object while checking public reflection and
    // original APK class_data. This method never reads private ART metadata.
    static synchronized Inputs create() throws Exception {
        if (application == null) throw new IllegalStateException("Bootstrap Context unavailable");
        ApplicationInfo info = application.getApplicationInfo();
        ArrayList<String> paths = new ArrayList<>(); paths.add(info.sourceDir);
        if (info.splitSourceDirs != null) for (String path : info.splitSourceDirs) paths.add(path);
        ArrayList<byte[]> dex = new ArrayList<>();
        for (String path : paths) try (ZipFile apk = new ZipFile(path)) {
            Enumeration<? extends ZipEntry> entries = apk.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry.getName().matches("classes(?:[2-9]|[1-9][0-9]+)?\\.dex"))
                    dex.add(read(apk.getInputStream(entry)));
            }
        }
        byte[] original = read(application.getAssets().open("artdcevm/bootstrap/original.dex"));
        byte[] additive = read(application.getAssets().open("artdcevm/bootstrap/additive.dex"));
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(original), null);
        Class<?> target = Class.forName("com.github.ytlog.artdcevm.internal.BootstrapAdditive", true, loader);
        byte[] cacheDex = read(application.getAssets().open("artdcevm/bootstrap/cache.dex"));
        return new Inputs(dex.toArray(new byte[0][]), target, additive, cacheDex);
    }
}
