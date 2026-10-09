# Borrowing the actual ART final native window

Android 11 and 12 declare `Thread::TransitionFromRunnableToSuspended` and
`TransitionFromSuspendedToRunnable` as `ALWAYS_INLINE`. The fixed
`ScopedThreadSuspension` constructor/destructor inline these operations. Auditing
both the primary ELF defined-name inventory and `.gnu_debugdata` names of six
captured API 30/31 devices (Huawei, Xiaomi, Pixel 5, vivo, Pixel 6, HONOR), twelve
ART/JVMTI libraries total, found no independently named transition or suspension
constructor/destructor definitions. This is evidence for those captured modules,
not an SDK rule. Legacy `JniMethodStart`/`JniMethodEnd` are not a pure replacement:
the return path restores JNI local-reference cookies and pops a handle scope.

Fixed sources:

- [Android 11 thread declarations](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/thread.h)
- [Android 11 suspension implementation](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/scoped_thread_state_change-inl.h)
- [Android 12 suspension implementation](https://android.googlesource.com/platform/art/+/refs/tags/android-12.0.0_r1/runtime/scoped_thread_state_change-inl.h)
- [Android 11 source transaction](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/openjdkjvmti/ti_redefine.cc)

## Ownership API

`Service::AuthorizeOriginalFinal` consumes the existing sealed complete ordinary
source calibration and exact original Run identity, without demanding a directly
callable transition pair. It does not authorize a synthetic stop.

The sole actual Run owner calls `Session::PrepareOriginalFinal` in Runnable,
then `OriginalCollected` only after the exact original driver's complete actual
Holder collection succeeds. Preparing or collecting grants no stop permission.
The shared hook opens the borrowed window only after the genuine named
`ScopedSuspendAll` constructor returns normally, with the calibrated final
cause/long-suspend pair, actual moving-GC-disable return, same current thread,
same mutator receiver and independently calibrated Native state. The fixed ART
constructor owns global suspension and exclusive mutator access. No lock,
thread-state or suspend-counter writes are simulated.

`IsFinalWindow`/`IsStopped` continuously require those identities and Native
state. `IsCollectedStop` remains false. `Reserved` remains false: this Session
owns neither a GC sentinel nor the original carrier. The destructor hook
revokes the window before the original destructor resumes threads. Completion
requires its actual return and the original moving-GC-enable return in Runnable.
A mismatched lifecycle permanently refuses the witness. Session destruction
never destroys a borrowed ART carrier or restores an ART-owned transition.

No JNI, managed allocation or native allocation occurs in this API. All graph,
copy, frame and reflective preparation must be completed by its consumer before
this final window. This is a needed ownership component, not complete schema
capability or permission to omit final graph/frame checks.

## Transaction boundary still required

In fixed AOSP Run, collection success is followed by DexCache registration,
boot-Dex publication if applicable, and breakpoint removal before the final stop.
The source explicitly treats the post-collection path as unable to fail safely.
Consequently a final closure rejection cannot simply return, throw, longjmp or
be called refundable. A closure scan done before threads resume is also not
proof of the final live frames.

A production consumer must either maintain a legitimately acquired continuous
reservation, or implement a separately proved transaction that prepares all
storage in the refundable phase and makes final mutation infallible after its
last admissible validation. Borrowing this window alone provides neither
rollback for published caches/breakpoints nor that infallibility. Invoking an
entire original Run directly in Native is invalid because its ordinary body
requires Runnable. Carrying a stop reservation across an ordinary JNI return
also remains invalid: instrumentation/deoptimization/flip paths can execute.

## Verification

The existing source-stop host runner exercises the new legacy high-16-bit
borrowed path with allocation forbidden, unchanged StartGC/mutator-lock counts,
exact original ctor/dtor callbacks, revocation before resume, and wrong Run,
driver, cause, repeated collection, current thread, mutator and state negatives.
It also retains all existing continuous-owner, rollback, cleanup, alias and
whole-process walker tests. The actual source-stop and dependent translation
units are separately compiled by the NDK for ARM64. No new device compatibility
claim follows from these host tests.
