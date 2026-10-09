# Atomic member batches: stable precommit boundary

This implements the removal/body-replacement batch from `742cfe3`; it does not
enable additions, hierarchy changes, arbitrary modifier changes, or full DCEVM.
The production multi-class entry requires a proven shared Direct gate. Actual
ONLOAD is one route. For the current LIVE app, JNI_OnLoad maps the plugin without
initializing it, installs the gate, then reads the named non-reset plugin lifetime
witness. Only a still-null witness proves there was never a historical environment.
An already initialized plugin preserves public/single-class support and reports
`atomicMemberBatch:false`; an empty environment registry alone is insufficient.

## Fixed source and the race

Android 11's fixed ART commit
[`34f62af4d6425087667b7eb3998fcf58d61a2fa6`](https://android.googlesource.com/platform/art/+/34f62af4d6425087667b7eb3998fcf58d61a2fa6/openjdkjvmti/ti_redefine.cc)
declares the static six-argument `RedefineClassesDirect`, boolean
`CheckAllClassesAreVerified(RedefinitionDataHolder&)` and
`CollectAndCreateNewInstances(RedefinitionDataHolder&)`, and
`RecordFailure(jvmtiError,const std::string&,const std::string&)`.
The same declarations and relevant order were reviewed in the locally cached
fixed 12/12.1/13/14/15/16/17 source tags. Symbol lookup never uses a SDK or BuildID
route. Platform vectors/strings are borrowed; the NDK does not own their STL
storage. The failure call borrows the existing fixed little-endian libc++ string
representation synchronously; it neither destroys nor retains it.

In the fixed 11 source, `Run` pauses class definitions at line 2476, calls the
verification boolean at 2480, and calls the instance-collection boolean at
2485. Registration of dex caches begins after the explicit irreversible
boundary at 2489. Exclusive suspension is only acquired at 2508. Refusal at
`UpdateClassStructurally` is therefore too late.

`ClassDefinitionPauser` only blocks new definitions by other threads. It allows
the redefining thread to define classes, has empty ClassLoad/ClassPrepare
callbacks, and does not serialize another `RedefineClassesDirect`. The apparent
TOP-level-lock comment is not an implemented global mutex: each input
`ClassRedefinition` only holds its own Class monitor. Non-input descendants are
not locked. Consequently a public loaded-class snapshot, native pointer equality
at one instant, or an exclusive check after the irreversible boundary is
insufficient. Acquiring every application Class monitor introduces lock-order
deadlocks and is deliberately not the synchronization mechanism.

## Implemented protocol

1. Before Debug.attachJvmtiAgent, the existing Java path loads artdcevm.
   JNI_OnLoad keeps libopenjdkjvmti mapped and publishes the named Direct hook.
   Only afterwards it reads named `openjdkjvmti::gEventHandler` using the OS
   self-memory API (`process_vm_readv` on Android), accepting only a complete
   naturally aligned pointer. This avoids a C++ load racing ART's ordinary store.
   Missing/denied kernel access refuses activation. Fixed
   [OpenjdkJvmTi.cc](https://android.googlesource.com/platform/art/+/34f62af4d6425087667b7eb3998fcf58d61a2fa6/openjdkjvmti/OpenjdkJvmTi.cc)
   zero-initializes this global, assigns it before registering GetEnvHandler,
   and explicitly leaves it allocated and non-reset at Deinitialize. A null
   value after publication excludes all prior environments, including disposed
   ones. A concurrent later Initialize is safe because Direct is already gated.
   Non-null/missing witness refuses activation. The image stays mapped to prevent
   lifetime-witness recycling. Early callers before original trampoline publication
   receive NOT_AVAILABLE instead of dereferencing an uninitialized callee.
   Actual public ONLOAD remains a separate supported proof. Failure preserves
   the public backend. This does not assume Dispose waits for other native calls:
   [the JVMTI lifecycle contract](https://docs.oracle.com/en/java/javase/17/docs/specs/jvmti.html#DisposeEnvironment)
   allows already running event handlers to continue after environment disposal;
   fixed ART Remove/delete also has no active-call counter. Merely holding an
   empty current registry therefore cannot establish historical quiescence.
2. The Direct hook takes a nonblocking common gate before the original acquires
   any Class monitor. It forwards all six arguments and the original result.
   Competing public redefine/retransform/direct entries and recursive entries
   return NOT_AVAILABLE. A production lease holds the same gate from before
   public candidate staging until final proof/root cleanup. Arbitrary native
   calls bypassing the normal fixed Direct entry are outside this contract.
3. Production enumerates a candidate loaded-class graph and independently
   proves every affected Class's DEX provenance, public declarations, native
   method identities, all reflected fields and contextual JNI field identities.
   Each target records its own `field_proof_base`; graph Class/mirror strong refs
   live only for this operation. Unknown loader/DEX provenance, incomplete
   declarations, or a public-stage exception refuses the entire batch.
4. After original verification succeeds, the real native Holder handle is
   barrier-read and immediately rooted with the already named AddGlobalRef ABI.
   Managed arrays are then traversed with pure JNI array operations. All actual
   initial-island oldClasses must form a duplicate-free bijection with the
   candidate Classes. Pure JVMTI loaded-class enumeration and JNI assignability
   additionally detect a child the defining thread loaded after ART captured
   oldClasses. No FindClass, Java reflection invocation, GetFieldID, class
   initialization, or new class-loading API runs in the window.
5. Each Class's current public field-ID set, complete native selected-field
   count, declaring Class and actual native membership must still match the
   staged reflected/contextual proofs. The proven Direct gate prevents another normal
   redefinition from changing these tables after staging. Existing JNI globals
   keep managed objects alive; raw managed pointers are decoded afresh and never
   held across JNI calls.
6. After original instance collection succeeds, the same closed-graph proof
   runs a second time at the last rejectable boolean. This catches classes
   created by verifier or allocation callbacks on the defining thread. Failure
   records the real ART driver result and returns false, so original Run exits
   before registering any new dex cache. Native allocation failure is caught
   within the hook and recorded as OUT_OF_MEMORY. Original verification failures
   and pending JNI exceptions remain intact. The source's subsequent registration
   and breakpoint work does not invoke Java class loading before final suspension.
7. Only a twice-sealed batch may enter the existing single whole-plan commit.
   The source retains each actual root array/index; all native tables and roots
   receive the existing final readonly validation before one BeginCommit.
   Every caller definition is recorded only after the whole source entry succeeds.
   Failure never publishes partial new provenance.

## Validation and remaining deployment work

Actual C++ Direct-hook and source-window translation units were tested with
independent JNI/JVMTI, native ELF-callee and low-address heap boundaries. The
window suite executes the actual batch callbacks and original common commit
implementation; the production public/native staging code additionally passed
ARM64 syntax checks and existing provenance/capability tests. The window's
provider callback is a boundary double, not a claim that the complete production
engine has run on a device.

The tests cover pristine plugin success, initialized/disposed history refusal,
initialization races, unpublished-original safety, ONLOAD/LIVE and phase-change refusal, six-argument forwarding,
nonblocking competing entries, recursion, raced/duplicate/missing Class roots,
Class identity mismatch, stale field refusal, last-collection refusal, original
verification-result propagation and zero publication on rejection. A forbidden
FindClass boundary verifies that the window does not invoke class loading.
All 16 stored real-device libopenjdkjvmti ELFs (API 30--37) expose the four named
gate/window/failure symbol families; symbol presence is not execution proof.

No APK build or device installation was performed for this batch. A real pristine-plugin or owned
startup deployment, multi-class retained/deleted member behavior, non-input
descendants, concurrent public JVMTI rejection, GC/root migration and loader
unloading still require the next unified device freeze. The actual app JNI_OnLoad path, named witness and Android self-memory permission
require device confirmation; already initialized LIVE plugins do not gain atomic
capability. The earlier host run passed before the OS-read helper was added. The
final helper received ARM64 syntax verification, but its host rerun and repeated
window/provenance suites are pending: new Mach-O processes (including unrelated
Homebrew git --version) hang at startup and do not respond to SIGKILL. This is
recorded as a verification limitation, not a new host PASS.
