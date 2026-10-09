# Independent physical caller calibration

`active_frame_recovery_proof::Service::ProvePhysical` verifies three distinct
physical compiled Java callers under the caller's real exclusive/suspended ART
scope. `PhysicalOracle` supplies each exact native method, the independently
parsed CodeItem register count and invoke DexPc, and distinct integer/reference
witnesses. Reference witnesses must be freshly decoded from strong roots inside
that stop; neither the service nor its result retains them.

The original named StackVisitor constructor, walk, GetMethod/GetDexPc,
CodeInfo constructor/stack-map lookup, JIT ContainsPc, native vreg readers and
architecture Context are reused. Fixed source declarations for Android 11–17
remain candidates; actual constructor echoes, method owners, native header/PC
ownership, CodeInfo geometry, complete register locations, GC root masks and
both native slow/physical value reads must agree. The physical caller chain
must occur innermost-to-carrier, on three distinct quick frames. All three
must supply a complete witness; a callback count alone never grants success.

The existing carrier/two-inlinee API remains independent and strict. A
three-physical result cannot satisfy its inline requirement. This distinction
is necessary for debuggable compilation, where the owned caller methods can
remain separate physical frames. The public Java debug query is diagnostic
only and never chooses a successful shape or grants readiness.

Debug self-instrumentation can request this exact scope with:

```sh
python3 tools/run-runtime-access.py --serial DEVICE --apk FROZEN_APP.apk \
  --runtime-services --jit-receivers --physical-active-frames
```

`wholeProcessFrameEligibility` stays false. Production integration must still
provide an independently calibrated selected visitor declaration, original
source stop lifetime, and a readonly all-thread observer that rejects quick
frames without the actual recoverable/deoptimization prerequisites. Three
owned witnessed callers do not prove arbitrary AOT/Nterp/quick-frame readiness.
No SDK, vendor or ELF BuildID chooses a declaration or waives a witness.

The fixed `runtime/instrumentation.cc` source semantics already explain the
production commit operation: Android 11–13 `DeoptimizeAllThreadFrames` installs
thread stacks and advances the source force-deopt generation; Android 14–17
calls `InstrumentAllThreadStacks(force_deopt=true)`. The modern commit delegates
to that named leaf under the original structural transaction's final exclusive
stop. It must not be replaced with per-thread flag/offset writes. In Android
14–17, `InstrumentationInstallStack` only marks quick frames whose header has
the native should-deoptimize flag; the source explicitly describes the
started-debuggable prerequisite that avoids AOT/Nterp execution. This module
observes owned JIT frames, rejects actual Nterp headers, and does not assert
that broader prerequisite.

Fixed-source references are the repository-cached release files under
`build/evidence/compatibility/20261008/modern-frame-source/android-*.0.0_r1/`,
with corresponding online sources at
[Android 11 instrumentation](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/instrumentation.cc),
[Android 13 instrumentation](https://android.googlesource.com/platform/art/+/refs/tags/android-13.0.0_r1/runtime/instrumentation.cc),
[Android 14 instrumentation](https://android.googlesource.com/platform/art/+/refs/tags/android-14.0.0_r1/runtime/instrumentation.cc), and
[Android 17 instrumentation](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/runtime/instrumentation.cc).

The slow-reader boundary now requires original `CodeInfo::DecodeDexRegisterMap`
with the complete fixed `DexRegisterMap` declaration (count, sixteen inline
locations, and a vector subclass without additional fields). Its entire output
must match the bounded source decoder before any original slow value read.
Current named GetMethod/GetDexPc and the callback's header/PC/quick frame/inline
range are rechecked immediately before each witness read. Host slow-reader
fixtures consume that actual visitor cursor and reject drift, rather than using
only the passed method as a value lookup key.

This is diagnostic hardening following API35/36 crashes inside the original
slow reader. No new physical-device result yet demonstrates that crash resolved.
The complete native decoder remains a fixed-source ABI candidate and must be
calibrated on each actual runtime; named availability alone grants no readiness.
