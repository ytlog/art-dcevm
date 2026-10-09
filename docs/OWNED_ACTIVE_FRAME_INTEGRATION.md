# Owned compiled-frame oracle integration

This batch is separate from the current contextual field proof batch. It does
not change production capabilities or claim full evolution. It needs the actual
owned JNI/method/runtime/JIT receiver bootstrap already performed by
RuntimeAccessSuite, and the loaded debug artdcevm library.

The optional caller delta now adds this source to the artdcevm target's
existing source list, after
`platform/jni_access/owned_probe.cc`:

```cmake
    platform/common/owned_active_frame_probe.cc
```

With `--active-frames`, RuntimeAccessSuite invokes the independent fixture
immediately after successful `probeJitServices` receiver calibration, before any
private evolution transaction:

```java
report.put("ownedActiveFrameProof", OwnedActiveFrameCalibration.run(apkPath));
```

The fixture throws on any missing/mismatched real proof and releases/joins its
native-blocked worker in finally. The suite catches this independent failure and
records `ownedActiveFrameProof.passed=false` with the actual diagnostic, while
continuing the independent method/field evidence. The runner rejects this report
when `--active-frames` was requested, and preserves `probe.json` plus
`ownedRemovalPassed` when method/field validation completed successfully.
Unrequested frame proof failure does not change the method/field scope. The
requested instrumentation enables public JVMTI/runtime/JIT receiver prerequisites
without implicitly requesting deletion transactions. The strict runner also
requires `activeFramesRequested == true` in the current instrumentation report
and nested `error == ""`. It requires nested `ownedActiveFrameProof.passed == true`, `carrierJitCompiled == true`,
`frames == 3`, `integers == 3`, `references == 3`, `locationReads == 6`,
`dexPcChecks == 3`, `inlineFlagNativeObserved == true`,
`blockedLiveWitnesses == true`, `retainedLiveWitnesses == true`, and
`warmCalls == 100000`, using strict JSON types. Those exact predicates must be
verified before including active-frame compatibility in reported scope. Current
method/field gates cannot substitute for this proof.

The Java fixture runs carrier -> inlinee1 -> inlinee2 -> native awaitGate. Each
level writes its distinct int/object pair into observable volatile slots after
its child returns. Slots are cleared after warming, must remain empty while the
worker is blocked, and must match the three original object identities and
integer values after release/join. The identity-hash checksum is additional
retained-result evidence; it is not the reference liveness oracle. Reflection warming attempts
100000 actual carrier calls; it does not imply optimization or inlining. Native
ContainsMethod verifies actual compiled carrier cache membership, and the native
walk must see that carrier and both distinct inline methods on the same machine
frame before maps/register/root witnesses can pass. If compiler policy does not
inline both methods, the error is explicitly Missing actual compiled carrier or
two distinct inlinees. Further tuning must use device observations of that exact
failure, not manufacture an inline proof.

MethodDexOracle verifies the installed APK identity/signature/class_data. The
new ActiveFrameDexOracle reads actual code_off/registers_size/ins_size and derives
incoming parameter indices from those CodeItems, rather than guessing D8 vreg
allocation. It walks instruction boundaries using the fixed
[official DEX formats](https://source.android.com/docs/core/runtime/dalvik-bytecode)
and records the unique invoke-static/range whose indexed target is each actual
child (including native awaitGate). Unknown/unused opcodes, payloads, truncated
instructions and ambiguous/missing child calls reject. All three methods have
six single-width static parameters. Native GetDexPc(false), decoded StackMap or
InlineInfo DexPc, and that independent invoke PC must all agree for each frame.
The fixed StackVisitor semantics report the invoke dex PC at this native call;
arbitrary return PCs are not accepted. The native WalkStack emits the two inline
callbacks only when both its header and stack map have inline information. The
decoded fixed kHasInlineInfo bit (1 << 0 in all seven declarations) must agree
with that independently observed condition. Other flags remain unqualified.

The native leaf captures the genuine CurrentFromGdb Thread pointer while the
worker is executing native awaitGate. No Java Thread field is read. The pointer
is borrowed only while that native worker remains blocked, then cleared on gate
release. Method native storage stays pinned by owned reflected Method globals;
managed witness identities are decoded again from independent strong globals
only inside the shared exclusive_owned_roots.With boundary, and never retained
across GC. Native Context is created and reset by ART; walking fills its callee
saves. Decoder source models are selected from constructor input echo geometry
and actual map/value/root oracle results, not Android API or BuildID. Maximum
supported fixed native visitor base space is reserved before callback state, and
a wrong family/context echo rejects before native walking. This covers the fixed
11/12/13/14/15/16/17 declarations; it does not establish arbitrary larger vendor
constructor declarations.

Android15 now has an explicit source-family label/declaration: decoded CodeInfo
storage reuses13 (344 bytes/three method columns), register reader reuses14's
boolean slow overload. Matching storage never grants14/15/16's distinct CodeInfo
flags or deoptimization semantics. The report identifies a proven storage/call
model, not a guessed Android version. Missing symbols or source geometry are
MissingProof failures.

Host tests compile the real JNI translation unit and Java fixture, exercise the
native gate with a real host worker, check actual CodeItem bit/offset layouts,
and verify native reader/constructor mismatch rejection under ASan/UBSan. They
are not Android activity/compiled-frame evidence. No APK or device run was
performed in this development batch; device qualification remains outstanding.

The independent runner accepts `--active-frames`; the device matrix accepts the
same optional flag and forwards it only to each runtime runner, not formal
production. The matrix defaults to existing method/field scopes. Only after the
strict frame proof passes does runtime metadata include the additional owned
compiled carrier/two-inlinee scope; full evolution remains unestablished.

Caller delta host validation uses actual Java suite/instrumentation compilation,
executes the actual optional catch/continuation block with injected frame success
and failure, exercises strict report mutations and the real runtime CLI through
fake ADB installation/hash/instrumentation, and checks actual matrix CLI optional
forwarding/defaults. No Android device observation is replaced by these tests.
