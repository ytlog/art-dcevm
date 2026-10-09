package com.github.ytlog.artdcevm.testing;

/** Public JNI table/ID cache created before production evolution; no ART private calls. */
final class ProductionPermissionNative {
    private ProductionPermissionNative() { }
    static native void retain(Class<?> owner,Object instance,String instanceField,String staticField);
    static native int read(boolean statik);
    static native void write(boolean statik,int value);
    static native void release();
}
