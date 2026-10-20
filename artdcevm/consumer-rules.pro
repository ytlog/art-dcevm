# Native methods are bound by their JNI names.
-keep class com.github.ytlog.artdcevm.api.DcevmRuntime { *; }
# Reflective Field objects of retired fields are remapped to these fields by name.
-keep class com.github.ytlog.artdcevm.internal.RetiredField { *; }
-keep class com.github.ytlog.artdcevm.internal.RetiredStaticField { *; }
# Native FindClass links this probe after the IMT hooks install. Keep the
# complete interface contract so R8 cannot change the IMT slot coverage.
-keep class com.github.ytlog.artdcevm.internal.ImtLinkProbe { *; }
-keep class com.github.ytlog.artdcevm.internal.ImtLinkProbeMethods { *; }
# The linker calls this preloaded native entry to verify each callback thread.
-keep class com.github.ytlog.artdcevm.internal.InterfaceLinkProbe { *; }
# Native startup verifies lookup arguments through this exact Java contract.
-keep class com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe { *; }
-keep interface com.github.ytlog.artdcevm.internal.MethodHandleLookupProbe$StaticOwner { *; }
# Native preparation verifies the bootstrap BindTo entry through this probe.
-keep class com.github.ytlog.artdcevm.internal.BoundMethodHandleProbe { *; }
-keep interface com.github.ytlog.artdcevm.internal.BoundMethodHandleProbe$Contract { *; }
-keep class com.github.ytlog.artdcevm.internal.BoundMethodHandleProbe$Target { *; }
