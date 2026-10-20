import java.lang.instrument.ClassDefinition;
import java.lang.instrument.Instrumentation;
import java.nio.file.Files;
import java.nio.file.Path;
import com.github.ytlog.artdcevm.fixture.JniFrameFixture;
import com.github.ytlog.artdcevm.fixture.JniFrameGate;
import com.github.ytlog.artdcevm.fixture.JniFrameProbe;

/** The public JNI probe and Java gate are shared verbatim with the Android test. */
public final class JniFramesOracle {
    private static Instrumentation instrumentation;

    public static void premain(String ignored, Instrumentation value) { instrumentation = value; }

    private static void change(Class<?> type, Path directory, boolean abort, boolean reverse,
                               String prefix) throws Exception {
        byte[] peer = Files.readAllBytes(directory.resolve(
                JniFrameFixture.Peer.class.getName().replace('.', '/') + ".class"));
        if (abort) peer[0] = 0;
        ClassDefinition targetDefinition = new ClassDefinition(type, Files.readAllBytes(
                directory.resolve(type.getName().replace('.', '/') + ".class")));
        ClassDefinition peerDefinition = new ClassDefinition(JniFrameFixture.Peer.class, peer);
        try {
            instrumentation.redefineClasses(reverse
                    ? new ClassDefinition[]{peerDefinition, targetDefinition}
                    : new ClassDefinition[]{targetDefinition, peerDefinition});
        } catch (Throwable error) {
            if (!abort) throw error;
            System.out.println(prefix + "failure=" + error.getClass().getName());
            return;
        }
        if (abort) throw new AssertionError("Missing atomic batch failure");
    }

    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        boolean own = Boolean.parseBoolean(args[3]), interfaceOwner = Boolean.parseBoolean(args[4]);
        boolean nonvirtual = Boolean.parseBoolean(args[5]), nativeHold = Boolean.parseBoolean(args[7]);
        int form = Integer.parseInt(args[6]);
        Object receiver = own ? new JniFrameFixture.Own() : new JniFrameFixture.DefaultOnly();
        Class<?> type = receiver.getClass();
        JniFrameFixture.Peer peer = new JniFrameFixture.Peer();
        int identity = System.identityHashCode(receiver);
        JniFrameProbe.cache(interfaceOwner ? JniFrameFixture.Contract.class : type);
        System.out.println("threadPolicy=" + JniFrameProbe.prepareThreadPolicy());
        System.out.println("before=" + JniFrameGate.observe(
                () -> JniFrameProbe.invoke(receiver, nonvirtual, form, false)));
        for (int round = 0; round < 2; ++round) {
            JniFrameGate.Parked[] frames = new JniFrameGate.Parked[4];
            try {
                for (int i = 0; i < frames.length; ++i) frames[i] = new JniFrameGate.Parked(
                        () -> JniFrameProbe.invoke(receiver, nonvirtual, form, nativeHold), i);
                for (JniFrameGate.Parked frame : frames) frame.awaitHeld();
                System.out.println("r" + round + "held=" + frames.length);
                for (int phase = 0; phase < 4; ++phase) {
                    String prefix = "r" + round + "p" + phase + ".";
                    change(type, Path.of(args[phase < 2 ? 1 : 2]), phase % 2 == 0,
                            round == 1, prefix);
                    System.out.println(prefix + "interface=" + JniFrameFixture.Contract.class.isInstance(receiver));
                    System.out.println(prefix + "peer=" + peer.value());
                    System.out.println(prefix + "identity=" + (identity == System.identityHashCode(receiver)));
                    System.out.println(prefix + "self=" + (type.getField("self").get(receiver) == receiver));
                    System.out.println(prefix + "kept=" + type.getField("kept").getInt(receiver));
                    System.gc(); System.runFinalization(); System.gc();
                    System.out.println(prefix + "gcIdentity=" + (identity == System.identityHashCode(receiver)));
                    System.out.println(prefix + "gcSelf=" + (type.getField("self").get(receiver) == receiver));
                    System.out.println(prefix + "gcInterface=" + JniFrameFixture.Contract.class.isInstance(receiver));
                    System.out.flush();
                    System.out.println(prefix + "frame=" + frames[phase].finish());
                    System.out.flush();
                    System.out.println(prefix + "followup=" + JniFrameGate.observe(
                            () -> JniFrameProbe.invoke(receiver, nonvirtual, form, false)));
                    System.out.flush();
                }
            } finally {
                for (JniFrameGate.Parked frame : frames) if (frame != null) frame.release();
                for (JniFrameGate.Parked frame : frames) if (frame != null) frame.close();
            }
        }
    }
}
