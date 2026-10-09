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

Only during the exact original CheckClass call can the first error 66 from the
same driver and the same signature address be deferred. All other errors go
straight to the original RecordFailure. A second 66 also goes straight through.
Another class error in the same driver invalidates the deferred success without
overwriting that error. Recursive CheckClass calls mask interception and
invalidate the outer enhancement. The episode is thread-local and ends before
any graph revalidation, marker or fallback. Original false plus exactly one
deferred 66, no other failure and a second exact graph proof is required to call
the original `RecordNewMethodAdded`; its actual source structural byte must be
one. An unobserved/inlined RecordFailure never counts as authorization.

The source's single-def, Java class flags, name, constructor lock and original
CheckRedefinable checks are preserved. Class modifier transitions remain
unsupported. When a deferred proof/marker fails, the root-provided calibrated
source failure bridge records error 66 with a synchronous literal; no temporary
platform std::string is retained or mutated. Native bad_alloc is caught inside
the proxy and refunded to the real source OOM path, before source DEX cleanup.

This is an installed early-check owner, not a complete production hierarchy
transaction. Root Run scope integration, pre-allocation graph Stage, actual
allocator/Finish/Load/IDs/verification composition, cache private/readonly proof,
and final exact-plan seal/registry/BeginCommit must all be present. Until that
composition and actual device validation succeed, production must keep hierarchy
enhancement refused. Hook availability and these host tests do not establish
ART or device readiness.
