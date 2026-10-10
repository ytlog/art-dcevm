package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Member;

/** Public semantic samples; native code independently re-reads type and member identity. */
public final class MethodHandleReceiverOracle {
    private MethodHandleReceiverOracle() {}
    public interface Contract { int value(int argument); }
    public static class Base implements Contract {
        public int field;
        public static int staticField;
        public int value(int argument) { return argument + 1; }
        private int direct(int argument) { return argument + 2; }
        public static int staticValue(int argument) { return argument + 3; }
        static MethodHandle directHandle() throws ReflectiveOperationException {
            return MethodHandles.lookup().findSpecial(Base.class, "direct",
                    MethodType.methodType(int.class, int.class), Base.class);
        }
    }
    public static final class Child extends Base {
        static MethodHandle superHandle() throws ReflectiveOperationException {
            return MethodHandles.lookup().findSpecial(Base.class, "value",
                    MethodType.methodType(int.class, int.class), Child.class);
        }
    }
    public static final class Sample {
        public final MethodHandle handle;
        public final Member member;
        public final int kind;
        Sample(MethodHandle handle, Member member, int kind) {
            this.handle = handle; this.member = member; this.kind = kind;
        }
    }
    public static Sample[] samples() throws ReflectiveOperationException {
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        MethodType signature = MethodType.methodType(int.class, int.class);
        return new Sample[] {
            new Sample(lookup.findVirtual(Base.class, "value", signature), Base.class.getMethod("value", int.class), 0),
            new Sample(Child.superHandle(), Base.class.getMethod("value", int.class), 1),
            new Sample(Base.directHandle(), Base.class.getDeclaredMethod("direct", int.class), 2),
            new Sample(lookup.findStatic(Base.class, "staticValue", signature), Base.class.getMethod("staticValue", int.class), 3),
            new Sample(lookup.findVirtual(Contract.class, "value", signature), Contract.class.getMethod("value", int.class), 4),
            new Sample(lookup.findGetter(Base.class, "field", int.class), Base.class.getField("field"), 8),
            new Sample(lookup.findSetter(Base.class, "field", int.class), Base.class.getField("field"), 9),
            new Sample(lookup.findStaticGetter(Base.class, "staticField", int.class), Base.class.getField("staticField"), 10),
            new Sample(lookup.findStaticSetter(Base.class, "staticField", int.class), Base.class.getField("staticField"), 11)
        };
    }
}
