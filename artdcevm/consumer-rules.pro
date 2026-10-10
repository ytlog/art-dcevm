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

# Ordinary JNI Native scope carrier is prebound by exact class/method names.
-keep class com.github.ytlog.artdcevm.internal.NativeScopeBridge { *; }

# The exception probe registers exact native names and calls throwSame from JNI.
-keep class com.github.ytlog.artdcevm.internal.JniExceptionUpcallFixture { *; }

# Native factory parses original installed DEX and resolves fixed caller/gate names.
-keep class com.github.ytlog.artdcevm.internal.CompiledFrameFixture { *; }
-keep class com.github.ytlog.artdcevm.internal.CompiledFrameFixture$Run { *; }

# Native initializer dispatch calibration permanently retires this private oracle.
-keep class com.github.ytlog.artdcevm.internal.InitializerGuardOracle { *; }

-keep class com.github.ytlog.artdcevm.internal.InitializerStateOracle** { *; }

-keep class com.github.ytlog.artdcevm.internal.InitializerEpisodeOracle** { *; }

-keep class com.github.ytlog.artdcevm.internal.InitializerWriterOracle** { *; }
