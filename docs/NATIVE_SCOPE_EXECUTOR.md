# Ordinary JNI Native scope executor

`platform/common/native_scope_executor` provides a synchronous real ordinary
JNI callback for a caller already in a proved Runnable scope. It also works
when the selected fixed Thread declaration has no named pure transition pair.
It does not call ART JNI cookie helpers, write Thread state, suspend threads,
reserve GC, or grant schema/source-stop authority.

## Entry and lifetime

During ordinary Native JNI initialization, `Executor::Prepare` uses the named
ART current-thread symbol and the supplied source-calibrated
`thread_suspend_service::Service` to require actual Native state. It binds
`NativeScopeBridge.enter(long)` with `RegisterNatives`, resolves its method ID,
and pins the carrier Class with a process-lifetime global reference. The Java
method is directly static native, without a wrapper, initializer, FastNative
or CriticalNative annotation; consumer rules retain its exact name.

Inside the existing real Runnable callback, call `Calibrate` before exposing
`WithNative`. The prebound `CallStaticVoidMethodA` invokes the ordinary native
method; ART owns its JNI cookie, local segment and HandleScope. Calibration
requires this same thread to enter actual Native state 92 and return to the
selected declaration's actual Runnable state. Every subsequent scope checks
these states again, plus the original environment, VM, current-thread identity,
readable extent, monotonic nonce and thread-local nonreentrant request.

Only the registered callback can construct the noncopyable `Context`.
`Context::Active` additionally requires pointer identity with the current
operation and current Native state. A boolean, stale pointer or foreign-thread
callback cannot create this authority. A callback mismatch never executes the
operation; duplicate entry invalidates that invocation. Existing Java exceptions
are rejected at entry and remain pending. C++ operation failures are caught
inside the JNI callback so ART can perform the real return sequence.

## Integration boundary

Prepare JNI references and allocation before `Operation`. Strongly register
all managed inputs before entering this bridge: its entry, public JNI preparation
and exit may suspend or collect. Once inside the operation, perform no JNI or
allocation. If a separately authorized local stop is acquired, refresh registered
roots after acquiring it and release every local stop and GC reservation before
returning or throwing. No borrowed Context or raw managed pointer may escape.
Use RAII for operation-owned resources on all rejection and exception paths.

The module does not implement or authorize a retained source-stop Session.
A future retained owner must independently prove real Native begin,
continuation, returned state and the original Run's final transfer. Merely
passing this executor's calibration or declaring an ordinary native method
cannot prove a continuous GC reservation or a no-safepoint exit. Keeping a GC
reservation across this JNI return is forbidden by this API.

## Fixed AOSP basis

In [Android 11 quick JNI entrypoints](https://android.googlesource.com/platform/art/+/android-11.0.0_r48/runtime/entrypoints/quick/quick_jni_entrypoints.cc),
`JniMethodStart` saves the local-reference cookie and transitions ordinary JNI
out of Runnable; `JniMethodEnd` returns to Runnable and pops local references
and HandleScope. The corresponding
[Android 12 implementation](https://android.googlesource.com/platform/art/+/android-12.0.0_r34/runtime/entrypoints/quick/quick_jni_entrypoints.cc)
and [Android 14 implementation](https://android.googlesource.com/platform/art/+/android-14.0.0_r22/runtime/entrypoints/quick/quick_jni_entrypoints.cc)
retain ART-owned normal JNI entry/return management. These helpers are source
evidence, never functions called manually by this executor.

[Android 11 JNI invocation](https://android.googlesource.com/platform/art/+/android-11.0.0_r48/runtime/jni/jni_internal.cc)
implements `CallStaticVoidMethodA` through ART's normal invocation path.
[ArtMethod::Invoke](https://android.googlesource.com/platform/art/+/android-11.0.0_r48/runtime/art_method.cc)
can process a deoptimization exception after invocation.
[Instrumentation](https://android.googlesource.com/platform/art/+/android-11.0.0_r48/runtime/instrumentation.cc)
can select instrumentation entry stubs for native methods and dispatch
MethodExit listeners on return. Such listeners may allocate or trigger GC;
native modifiers alone therefore cannot justify retaining a GC reservation.

## Verification limits

The actual production TU tests cover both fixed state declarations with missing
transition pairs, JNI registration/callback closure, all identity checks,
nonreentrancy, no-JNI/no-allocation operation, original exceptions, failures and
recovery under ASan/UBSan. A separate actual host JVM test uses real
RegisterNatives and ordinary JNI dispatch under `-Xcheck:jni`, including original
Java exception preservation. Its ART Thread-state/mapping fixtures remain
explicit doubles: this proves the host JNI call path, not Android runtime
calibration. Strict ARM64 compilation passes. Device calibration and root
Session integration are separate work; no device success is claimed here.
