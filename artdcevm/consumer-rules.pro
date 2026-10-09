# Native entry names and predefined bootstrap schemas are part of the JNI contract.
-keep class com.github.ytlog.artdcevm.api.DcevmRuntime { *; }
-keep class com.github.ytlog.artdcevm.internal.EvolutionCalibration { *; }
-keep class com.github.ytlog.artdcevm.internal.CalibrationFixture { *; }

-keep class com.github.ytlog.artdcevm.internal.EvolutionCalibration$InvocationContract { *; }

-keep class com.github.ytlog.artdcevm.internal.ProductionBootstrap { *; }
-keep class com.github.ytlog.artdcevm.internal.ProductionBootstrap$Inputs { *; }

# Public array behavior and runtime Class oracle invoked by the native VH owner.
-keep class com.github.ytlog.artdcevm.internal.VarHandleClassOracle { public static java.lang.invoke.VarHandle create(); }

-keep class com.github.ytlog.artdcevm.internal.AbstractVTableOracle { public static java.lang.Object[] create(); }
-keep class com.github.ytlog.artdcevm.internal.AbstractVTableOracle$* { *; }
# Actual installed-APK permission probes; native resolves exact public declarations.
-keep class com.github.ytlog.artdcevm.internal.FinalPermissionOracle { *; }
-keep class com.github.ytlog.artdcevm.internal.FinalPermissionOracle$Target { *; }
-keep class com.github.ytlog.artdcevm.internal.FinalPermissionOracle$Handles { *; }
