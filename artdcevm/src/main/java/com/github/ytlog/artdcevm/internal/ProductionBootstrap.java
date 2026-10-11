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
    // These live public oracles never participate in the bootstrap evolution
    // that intentionally retires CalibrationFixture's original int fields.
    static final class LazyFieldCalibration {
        public int instanceValue;
        public static long staticValue;
    }
    static final class Inputs {
        final Object lazyFieldCalibration = new LazyFieldCalibration();
        // The receiver Class stays local until native creates its exact strong root.
        Class<?> optionalFieldClass() throws Exception { return ProductionBootstrap.optionalFieldClass(); }
        final byte[][] dex;
        final Class<?> ordinarySourceClass;
        final byte[][] ordinarySourceDex;
        final Object ordinarySourceObject;
        final Class<?> additiveClass;
        final byte[] additiveDex;
        final Object storage;
        final byte[] cacheDex;
        final Class<?> cacheClass, cacheWriterClass;
        final Object cacheFixture;
        final Method cacheRead, cacheWarm;
        final byte[] receiverDex;
        final Object receiverSourceObject;
        final Object receiverGuardFixture;
        final Method[] receiverCallers = new Method[10], receiverTargets = new Method[10];
        final java.lang.reflect.Field cacheField;
        final FinalPermissionOracle.Target finalTarget = FinalPermissionOracle.create();
        final Object finalReferenceSentinel = new Object();
        final Method finalWriter;
        final Object methodReceiver = new EvolutionCalibration();
        final MethodHandle[] methodHandles;
        final Method[] methods;
        final Method[] receiverEntryMethods;
        final java.lang.reflect.Field[] receiverEntryFields;
        final Object[] receiverEntryObjects;
        final MethodHandleReceiverOracle.Sample[] receiverHandleSamples;
        final VarHandle instanceHandle, staticHandle;
        final MethodHandle instanceGetter, instanceSetter, staticGetter, staticSetter;
        Inputs(byte[][] dex, Class<?> target, byte[] replacement, byte[] cacheDex, byte[][] ordinaryDex, byte[] receiverDex) throws Exception {
            finalWriter = FinalPermissionOracle.Writer.class.getDeclaredMethod("write",
                FinalPermissionOracle.Target.class, int.class, Object.class, boolean.class, boolean.class);
            this.dex = dex; additiveClass = target; additiveDex = replacement;
            ordinarySourceDex = ordinaryDex;
            ClassLoader ordinaryLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(ordinaryDex[0]), null);
            ordinarySourceClass = Class.forName("com.github.ytlog.artdcevm.internal.OrdinarySourceFixture", true, ordinaryLoader);
            Object ordinary = ordinarySourceClass.getDeclaredConstructor().newInstance();
            ordinarySourceObject = ordinary;
            ordinarySourceClass.getDeclaredField("value").setInt(ordinary, 6);
            ordinarySourceClass.getDeclaredField("staticValue").setInt(null, 17);
            if ((int) ordinarySourceClass.getDeclaredMethod("compute").invoke(ordinary) != 7)
                throw new IllegalStateException("Private ordinary source fixture value changed");
            this.cacheDex = cacheDex;
            ClassLoader cacheLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(cacheDex), null);
            cacheClass = Class.forName("com.github.ytlog.artdcevm.internal.SourceCacheFixture", true, cacheLoader);
            cacheWriterClass = Class.forName("com.github.ytlog.artdcevm.internal.ZCacheWriter", true, cacheLoader);
            cacheFixture = cacheClass.getDeclaredConstructor().newInstance();
            cacheRead = cacheClass.getDeclaredMethod("read");
            cacheWarm = cacheClass.getDeclaredMethod("warm", cacheClass);
            cacheField = cacheClass.getDeclaredField("number");
            // The source calibration callback owns the first actual cold writer.
            this.receiverDex = receiverDex;
            ClassLoader receiverLoader = new InMemoryDexClassLoader(ByteBuffer.wrap(receiverDex), null);
            String receiverName = "com.github.ytlog.artdcevm.internal.ReceiverSourceFixture";
            Class<?> fixture = Class.forName(receiverName, true, receiverLoader);
            Class<?> base = Class.forName(receiverName + "$Base", true, receiverLoader);
            Class<?> concrete = Class.forName(receiverName + "$Receiver", true, receiverLoader);
            Class<?> contract = Class.forName(receiverName + "$Contract", true, receiverLoader);
            receiverSourceObject = concrete.getDeclaredConstructor().newInstance();
            receiverGuardFixture = receiverGuardFixture(receiverLoader);
            String[] kinds = {"Virtual", "Super", "Direct", "Static", "Interface"};
            for (int kind = 0; kind < 5; ++kind) for (int range = 0; range < 2; ++range) {
                int index = kind * 2 + range;
                String suffix = range == 0 ? "Small" : "Range";
                Class<?> callerOwner = kind == 1 || kind == 2 ? concrete : fixture;
                Class<?> targetOwner = kind == 1 || kind == 0 ? base : kind == 2 ? concrete : kind == 3 ? fixture : contract;
                Class<?>[] callerArgs = kind == 0 ? new Class<?>[]{base} : kind == 4 ? new Class<?>[]{contract} : new Class<?>[0];
                Class<?>[] targetArgs = range == 0 ? new Class<?>[]{int.class} : new Class<?>[]{int.class,int.class,int.class,int.class,int.class,int.class};
                receiverCallers[index] = callerOwner.getDeclaredMethod("call" + kinds[kind] + suffix, callerArgs);
                receiverTargets[index] = targetOwner.getDeclaredMethod(kinds[kind].toLowerCase() + suffix, targetArgs);
                receiverCallers[index].setAccessible(true); receiverTargets[index].setAccessible(true);
            }
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
            receiverEntryMethods = new Method[]{methods[0], methods[2]};
            receiverEntryFields = new java.lang.reflect.Field[]{
                CalibrationFixture.class.getDeclaredField("intValue"),
                CalibrationFixture.class.getDeclaredField("staticIntValue")};
            receiverEntryObjects = new Object[]{methodReceiver, storage};
            receiverHandleSamples = receiverHandleSamples();
            instanceHandle = lookup.findVarHandle(CalibrationFixture.class, "intValue", int.class);
            staticHandle = lookup.findStaticVarHandle(CalibrationFixture.class, "staticIntValue", int.class);
            instanceGetter = lookup.findGetter(CalibrationFixture.class, "intValue", int.class);
            instanceSetter = lookup.findSetter(CalibrationFixture.class, "intValue", int.class);
            staticGetter = lookup.findStaticGetter(CalibrationFixture.class, "staticIntValue", int.class);
            staticSetter = lookup.findStaticSetter(CalibrationFixture.class, "staticIntValue", int.class);
        }
    }
    private static Object receiverGuardFixture(ClassLoader receiverLoader) {
        try {
            return Class.forName("com.github.ytlog.artdcevm.internal.ReceiverGuardFixture", true,
                    receiverLoader).getDeclaredConstructor().newInstance();
        } catch (ReflectiveOperationException | RuntimeException | LinkageError unavailable) {
            return null;
        }
    }
    private static MethodHandleReceiverOracle.Sample[] receiverHandleSamples() {
        try { return MethodHandleReceiverOracle.samples(); }
        catch (ReflectiveOperationException | RuntimeException unavailable) { return null; }
    }
    static int runReceiverSource(Inputs input, int index) throws Exception {
        if (index < 0 || index >= input.receiverCallers.length) throw new IllegalArgumentException("receiver sample index");
        Method caller = input.receiverCallers[index];
        Object target = java.lang.reflect.Modifier.isStatic(caller.getModifiers()) ? null : input.receiverSourceObject;
        Object[] args = caller.getParameterTypes().length == 0 ? new Object[0] : new Object[]{input.receiverSourceObject};
        return (int) caller.invoke(target, args);
    }
    private static java.lang.reflect.Field finalWriteField(int index) throws Exception {
        if (index < 0 || index >= 4) throw new IllegalArgumentException("final writer sample index");
        String[] names = {"instanceValue", "staticValue", "instanceReference", "staticReference"};
        return FinalPermissionOracle.Target.class.getDeclaredField(names[index]);
    }
    static boolean prepareFinalWrite(Inputs input, int index) throws Exception {
        java.lang.reflect.Field field = finalWriteField(index);
        boolean statik = (index & 1) != 0, reference = index >= 2;
        FinalPermissionOracle.Writer.write(input.finalTarget, 41 + index,
            input.finalReferenceSentinel, statik, reference);
        Object target = statik ? null : input.finalTarget;
        return reference ? field.get(target) == input.finalReferenceSentinel
                         : field.getInt(target) == 41 + index;
    }
    // Two independent facts: the exact exception type and the unchanged public
    // reflection value. Native must separately witness the actual writer event.
    static int exerciseFinalWrite(Inputs input, int index) throws Exception {
        java.lang.reflect.Field field = finalWriteField(index);
        boolean statik = (index & 1) != 0, reference = index >= 2;
        int result = 0;
        try {
            FinalPermissionOracle.Writer.write(input.finalTarget, 48 + index, input, statik, reference);
        } catch (IllegalAccessError expected) { result |= 1; }
        Object target = statik ? null : input.finalTarget;
        if (reference ? field.get(target) == input.finalReferenceSentinel
                      : field.getInt(target) == 41 + index) result |= 2;
        return result;
    }
    static boolean verifyOrdinarySource(Inputs input, int step) throws Exception {
        Class<?> type = input.ordinarySourceClass;
        if (step < 0 || step > 2 || type.getDeclaredMethods().length != 1 + step ||
            type.getDeclaredField("value").getInt(input.ordinarySourceObject) != 6 ||
            type.getDeclaredField("staticValue").getInt(null) != 17 ||
            !verifyOrdinaryObject(type, input.ordinarySourceObject, step))
            return false;
        Object fresh = type.getDeclaredConstructor().newInstance();
        type.getDeclaredField("value").setInt(fresh, 6);
        return verifyOrdinaryObject(type, fresh, step);
    }
    private static boolean verifyOrdinaryObject(Class<?> type, Object object, int step) throws Exception {
        return (int) type.getDeclaredMethod("compute").invoke(object) == 7 &&
            (step < 1 || (int) type.getDeclaredMethod("second").invoke(object) == 9) &&
            (step < 2 || (int) type.getDeclaredMethod("first").invoke(object) == 8);
    }
    static Class<?> warmSourceCache(Inputs input) throws Exception {
        return (Class<?>) input.cacheWarm.invoke(null, input.cacheFixture);
    }
    static int invokeMethod(Inputs input) throws Throwable {
        return (int) input.methodHandles[0].invoke(input.methodReceiver);
    }
    static boolean exerciseMethodHandle(Inputs input, int index) throws Throwable {
        if (index < 0 || index >= input.methods.length)
            throw new IllegalArgumentException("method MethodHandle sample index");
        EvolutionCalibration receiver = (EvolutionCalibration) input.methodReceiver;
        int before = receiver.value;
        boolean statik = java.lang.reflect.Modifier.isStatic(input.methods[index].getModifiers());
        int expected = (int) input.methods[index].invoke(statik ? null : receiver);
        int actual = statik ? (int) input.methodHandles[index].invoke()
                            : (int) input.methodHandles[index].invoke(receiver);
        return actual == expected && receiver.value == before;
    }
    static int invokeField(Inputs input, int index) throws Throwable {
        return index == 0 ? (int) input.instanceGetter.invoke(input.storage)
                          : (int) input.staticGetter.invoke();
    }
    static boolean exerciseFieldMethodHandle(Inputs input, int index) throws Throwable {
        CalibrationFixture object = (CalibrationFixture) input.storage;
        int instance = object.intValue, statik = CalibrationFixture.staticIntValue;
        switch (index) {
            case 0: if ((int) input.instanceGetter.invoke(object) != instance) return false; break;
            case 1: input.instanceSetter.invoke(object, instance); break;
            case 2: if ((int) input.staticGetter.invoke() != statik) return false; break;
            case 3: input.staticSetter.invoke(statik); break;
            default: throw new IllegalArgumentException("field MethodHandle sample index");
        }
        return object.intValue == instance && CalibrationFixture.staticIntValue == statik;
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
    private static synchronized Class<?> optionalFieldClass() throws Exception {
        if (application == null) throw new IllegalStateException("Bootstrap Context unavailable");
        byte[] bytes = read(application.getAssets().open("artdcevm/bootstrap/missing-optional.dex"));
        ClassLoader loader = new InMemoryDexClassLoader(ByteBuffer.wrap(bytes), null);
        return Class.forName("com.github.ytlog.artdcevm.internal.MissingOptionalFields", false, loader);
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
        // Bootstrap with the smallest declaration, then add second and first
        // through ART's original structural extension. Calibration must not
        // depend on the private deletion commit engine it is establishing.
        byte[][] ordinaryDex = new byte[][] {
            read(application.getAssets().open("artdcevm/bootstrap/source-second.dex")),
            read(application.getAssets().open("artdcevm/bootstrap/source-first.dex")),
            read(application.getAssets().open("artdcevm/bootstrap/source-original.dex"))};
        byte[] receiverDex = read(application.getAssets().open("artdcevm/bootstrap/receiver.dex"));
        return new Inputs(dex.toArray(new byte[0][]), target, additive, cacheDex, ordinaryDex, receiverDex);
    }
}
