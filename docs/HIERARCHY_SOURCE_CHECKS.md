# Original source CheckClass ownership

`native_hierarchy_source_checks` preserves the original named `CheckClass`
trampoline. The fixed `android-{11,12,13,14,15,16,17}.0.0_r1`
`art/openjdkjvmti/ti_redefine.h` and `.cc` declarations were read independently.
They declare `Redefiner::RecordFailure(jvmtiError, const std::string&,
const std::string&)`; the ClassRedefinition wrapper forwards its own `driver_`
and `class_sig_`. This is a four-argument member-function carrier including
`this`, without varargs. The shared prefix places the signature at byte 24,
`added_methods_` at byte 65, and the driver's `self_`/`type_` at 24/32. The
family-dependent trailing class policy byte is never read.

The root's sole original Run owner must open `Scope` using its actual driver,
Runnable Context, independently calibrated source roots and staged graph.
The scope reads the actual owner's complete mapped StandardDex header,
requires one ClassDef, parses the actual immutable incoming declarations,
and checks the named `GetMirrorClass` against privately retained public inputs.
`Authority::CheckClass` must accept the complete exact incoming schema before
and after the original check. The actual driver must be the selected structural
driver, its self must match the context, and its result must still be INTERNAL.
The independently calibrated global decoder establishes the current Runnable
thread/JNIEnv before invoking source leaves. No version/vendor selector grants
permission.

During the exact original CheckClass call, only the first independently expected
diagnostic from the same driver and signature address can be deferred. Unchanged
flags select hierarchy error 66; an exact staged final/abstract delta selects
class-modifier error 71. All other diagnostics and repeats go straight to the
original RecordFailure. Another class error invalidates the episode. Recursive
checks mask interception and invalidate the outer enhancement. The thread-local
episode ends before revalidation, marker or refusal. Original false, exactly one
expected diagnostic, no other failure, a second complete declaration/native
graph proof and the actual `RecordNewMethodAdded` structural byte are required.
Unobserved/inlined failures never count as permission.

In all seven pinned sources, the single-ClassDef check precedes the modifier
comparison, which precedes name and hierarchy comparisons. Android 13/14 also
check `lock_acquired_` before modifiers. Those original checks and the separate
original CheckRedefinable path remain intact. A combined flag/hierarchy change
returns at 71, so the independent exact DEX descriptor and complete proposed
parent/interface graph prove the subsequent name/edge semantics. The replacement
allocator obtains flags from ART SetupClass/LoadClass using the incoming DEX;
actual linked flags are checked before private cache mutation and again through
the frozen metadata ReadContext before publication. Implicit descendants retain
their original flags. Proposed edges use proposed parent flags, rejecting final
parents and invalid final+abstract or role/access changes.

This early marker is preliminary permission to allocate, never permission to
publish. The native schema Plan must subsequently prove all required guards
against actual old/new Classes: adding final requires the independent loaded
subclass guard; adding abstract requires the stopped existing-instance census.
Their failure must refund the unpublished source transaction. Reflection,
cached-constructor instantiation and loaded-subclass legality still require live
whole-command evidence. A failed deferred proof/marker records its selected 66
or 71 through the original failure bridge. Native bad_alloc follows the source
OOM path before DEX cleanup; temporary platform strings are never retained.

This is an installed early-check owner, not a complete production hierarchy
transaction. Root Run scope integration, pre-allocation graph Stage, actual
allocator/Finish/Load/IDs/verification composition, cache private/readonly proof,
and final exact-plan seal/registry/BeginCommit must all be present. Until that
composition and actual device validation succeed, production must keep hierarchy
enhancement refused. Hook availability and these host tests do not establish
ART or device readiness.
