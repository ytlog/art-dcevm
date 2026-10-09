# Nondebuggable actual JIT frame target

This module runs an isolated same-signature instrumentation process whose application and test manifests request nondebuggable ART policy. It reuses the original Java DEX/fixture/frame oracle and native Context/CodeInfo proof. Its native bootstrap observes original JNI, JIT compiler completion and public Java GC rather than initializing the public JVMTI backend. Named ELF availability alone never grants frame recovery.

Build the complete frozen batch with:

```sh
./gradlew :frameproof:assembleProbe :frameproof:assembleProbeAndroidTest
python3 tools/run-nondebuggable-frameproof.py --serial DEVICE --adb /absolute/adb --apk /absolute/frameproof-probe.apk --test-apk /absolute/frameproof-probe-androidTest.apk
```

The runner verifies both requested/installed SHA-256 values and records the current structured proof under ignored `build/evidence/nondebuggable-frameproof/`. Runtime debug policy, actual JIT compilation, two distinct inlinees, installed DEX invocation PCs, six live vregs and retained identities must all pass. This is an owned fixture proof, not full evolution or arbitrary application frame recovery.

Fixed AOSP declarations for compiler completion (no version-based dispatch):

- [Android 11 jit_code_cache.h](https://android.googlesource.com/platform/art/+/android-11.0.0_r1/runtime/jit/jit_code_cache.h): `(ArtMethod*, Thread*, bool osr)`.
- [Android 12 jit_code_cache.h](https://android.googlesource.com/platform/art/+/android-12.0.0_r1/runtime/jit/jit_code_cache.h) and Android 13: `(ArtMethod*, Thread*, CompilationKind)`; the source enum uses `int`.
- [Android 14 jit_code_cache.h](https://android.googlesource.com/platform/art/+/android-14.0.0_r1/runtime/jit/jit_code_cache.h), Android 15/16 and the fixed Android 17 source `b753cf97923c3695338d21466fa14c57b480a59a`: `(ArtMethod*, Thread*)`.

Original callbacks supply actual receivers for the independently identified carrier. Runtime source candidates must match all seven observed receivers; neither platform API, vendor nor Build ID selects a capability. All observers preserve the original ABI/arguments/result. No debug-policy write or compiler-policy override is present.
