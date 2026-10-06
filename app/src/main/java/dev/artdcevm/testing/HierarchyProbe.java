package dev.artdcevm.testing;
public final class HierarchyProbe {
    private HierarchyProbe() {}
    public static native void retain(Object object);
    public static native long[] fields();
    public static native int invoke(boolean nonvirtual);
    public static native boolean instanceOf(Class<?> cls);
    public static native int narrowJniRead(Object receiver);
    public static native int narrowJniInvoke(Object receiver);
    public static native int narrowJniInvokeNonvirtual(Object receiver);
    /** form 0/1: virtual .../V; form 2/3: nonvirtual .../V. */
    public static native int narrowJniInvokeForm(Object receiver, int form);
    public static native void retainInterfaceMethod(Class<?> type);
    public static native int interfaceJniInvoke(Object receiver);
    /** form 0/1/2: CallIntMethod, CallIntMethodA, CallIntMethodV. */
    public static native int interfaceJniInvokeForm(Object receiver, int form, boolean fresh);
    public static native void releaseInterfaceMethod();
    public static native void retainDefaultInterfaceMethod(Class<?> type);
    /** form 0: virtual A; form 1/2/3: nonvirtual A/normal/V. */
    public static native int defaultInterfaceJniInvokeForm(Object receiver, int form, boolean fresh);
    public static native void releaseDefaultInterfaceMethod();
    public static native void release();
}
