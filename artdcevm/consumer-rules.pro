# Native methods are bound by their JNI names.
-keep class com.github.ytlog.artdcevm.api.DcevmRuntime { *; }
# Reflective Field objects of retired fields are remapped to these fields by name.
-keep class com.github.ytlog.artdcevm.internal.RetiredField { *; }
-keep class com.github.ytlog.artdcevm.internal.RetiredStaticField { *; }
