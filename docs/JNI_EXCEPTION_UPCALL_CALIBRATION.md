# JNI exception upcall calibration

`platform/common/native_jni_exception_upcall.h` is an ordinary JNI calibration
probe, not permission to retire a boot frame. The registrar must bind the four
native methods of the exact owned `JniExceptionUpcallFixture` class to one
thread-owned `jni_exception_upcall::Probe`.

`Prepare` retains the fixture class and expected Throwable, resolves that class's
`throwSame(Throwable)`, and binds a unique request to the original VM, JNIEnv and
native thread. `Invoke` calls the actual JNI `CallStaticVoidMethodA` and observes
`ExceptionCheck` / `ExceptionOccurred`. It deletes only the temporary local
reference and returns to Java with the original pending exception intact. It
never clears or rethrows the exception and never calls `IsSameObject` while an
exception is pending.

The fixture's actual Java catch passes its caught Throwable to `Complete` in the
next ordinary JNI invocation. Public `IsSameObject` compares that catch result to
the retained expected root. Completing a witness consumes the request, including
an incorrect identity or an incomplete invocation. Foreign threads, different
JNIEnv pointers, stale requests and replayed completions cannot consume it.
`Cancel` releases retained roots on the same original thread, including incomplete
witnesses; it does not clear pending exceptions.

A successful fixture run establishes this original JNI exception propagation
path. Production authorization still requires independent complete actual
ManagedStack terminal evidence, exact named invoke-stub extent/return provenance,
and the same transaction's managed/JNI root and dependency closure. This fixture
cannot discharge any of those requirements, nor prove an unobserved native caller
safe. Integration must use the actual owned Java catch body; supplying the expected
root as a fabricated `caught` argument does not constitute calibration evidence.

Validation: `python3 tools/test-native-jni-exception-upcall-host.py` compiles an
ASan/UBSan C++ adversarial test and runs the registered real native probe in a JVM
with `-Xcheck:jni`. The real JVM test runs 100 same-token witnesses, rejects replay
inside the native completion callback, and runs another independent native thread.
The C++ test additionally rejects absent/lost pending exceptions, wrong identity,
incomplete invocation, duplicate setup, foreign thread/env and stale requests.
Its JNI table deliberately has no `ExceptionClear` or `Throw` entries. Raw results
are under ignored `build/evidence/host/native-jni-exception-upcall/`.

These tests passed on the development host; they are not Android 16 device
verification. Device registration, execution and transaction integration remain
necessary before accepting this evidence on a device.
