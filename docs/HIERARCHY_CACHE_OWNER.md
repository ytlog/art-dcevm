# Incoming hierarchy cache ownership

`native_hierarchy_cache::Owner` is the typed cache participant of the rooted
hierarchy authority. It does not authorize a hierarchy operation by itself.
The source allocator must implement `SourceMapper` using its actual retained
Class/loader/DexCache roots and immutable incoming StandardDEX mapping. The
public JNI fixture owner must supply an independent resolved Class, named native
field and named native method witness. A method/field pointer inferred from a
cache being calibrated is not an independent witness.

Calibration tries the fixed `Pairs` and `DualArrays` declarations and ordinary
and sequence native pairs. Exactly one combination must reproduce all three
public identities. Both pair and full representations, when allocated, must
agree. Complete mapped spans are checked before accessing native storage.
Cache presence, API number, vendor, BuildID or an allocated full array alone
cannot establish the pair format. A platform which cannot expose independent
resolved pair witnesses is refused until it supplies a stronger proof.

The source mapper's `Describe` must establish the exact incoming DEX identity,
verified ID counts and Class definition index, and return a stable GC-updated
DexCache root and a private allocation token. `FindSource` must execute the
actual original source verifier Class mapping in the correct allocator context;
parsing the requested descriptor without obtaining the real mapped Class is
insufficient. Resolve all occupied type indices parent-first. Allocated root
slots remain owned through the original source transaction and failed Class/DEX
cleanup. Stage rejects a mapper whose real graph, allocator context or runtime
services have not been proved.

`Prepare` allocates a journal of the actual source mapped roots before writing.
It then rechecks cache array identities, occupancy and DEX roles after possible
root lookup safepoints. `OwnsUnpublished` must prove the current source mutator
writer window and exclusively owned, unpublished incoming cache; it must never
grant a published unrelated cache under shared mutator access. Within that
no-suspend window the owner replaces managed type roots, preserving pair index
and slot order, clears *both* allocated native field/method representations,
and calls the independently calibrated source DexCache write barrier. Ordinary
pairs use source InvalidIndexForSlot (slot zero = 1, other slots = 0). Sequence
pairs also advance the sequence while clearing the native value; a held lock is
refused rather than spinning within source allocation. Full arrays clear every
verified DEX member slot. A barrier failure leaves no prepared grant and requires
the original source operation to abort; its private allocations are retired by
the source allocator owner.

After collection and GC, `RefreshPrepared` reloads exact owner-retained old/new
Class, parent and cache roots. `Owner::Refresh` fixes existing records without
allocation. Final verification reads every populated managed slot against its
rooted expected Class and immutable type index, checks array identity and all
native slots, and requires the actual owner's `VerifyFrozen` and `VerifyCommit`.
These callbacks must prove source lifetime, public graph identity, actual final
exclusive boundary, reflective/resolution invalidation, heap replacement,
compiled deoptimization and interpreter cache clearing. They cannot grant a
service from an opaque readiness bit. Final callbacks may not allocate, invoke
JNI, parse DEX, suspend or mutate ART. `BeginCommit` runs only after that exact
verification succeeds.

The host test exercises the actual common fixed readers and rewrite/reset
engine with four declaration/format combinations, real native arrays and moved
managed cache storage. Public JNI/native-leaf Class mapping, barriers and source
runtime owner are explicit fixture doubles; these tests do not establish an
actual modern ART hierarchy allocation or source transaction. The production
source adapter must complete those independent proofs before enabling Stage.

## Actual incoming source mapper

`native_hierarchy_source_cache::Mapper` connects the cache participant to the
actual calibrated source `Pool`, `FieldMetadata` and `FieldStorage` services.
`BatchAccess` is the allocator's typed reader of retained old/new/cache/parent
root slots and immutable source DEX records. The adapter does not accept a
caller supplied cache ownership flag.

Stage reads each complete incoming StandardDEX (including shared 041 container
metadata), compares its complete Class declarations with the immutable staged
schema, verifies the cache's actual native DexFile prefix against that mapping,
and confirms the old Class belongs to the staged affected graph. The unused
DexFile size slot is never used as a mapping extent. Exact immutable DEX and
root slot provenance are checked again after preparation.

Describe requires an actual linked new Class with the same descriptor/loader as
the retained old Class, the precise incoming DexCache, source class index and
requested parent. The parent's native descriptor must match the incoming DEX
superclass. The cache must differ from the published original cache. The
independently calibrated original ClassLinker lookup in the allocator's actual
context must return the retained old Class, proving that the replacement has
not been published in that namespace. That ClassTable/source writer window and
allocation lifetime remain obligations of the source owner; checking one
lookup does not establish a lock or a later cleanup lifetime. The mandatory
`RequiredServices::VerifyPrivate` additionally requires independently observed
source allocation/publication hook ownership and the actual private-cache
no-suspend writer window; missing proof refuses the mutation.

Every populated type index is decoded with the shared immutable DEX reader.
The adapter calls the original native Class resolver first and immediately
roots its actual return, preserving source resolution failures and effects.
Ordinary Class descriptors are compared against the native returned identity.
Only then does it select an exact batch/ancestor replacement root in the
resolved Class's actual loader namespace, including loader delegation. An
array whose component is replaced requires an actual rooted replacement array
mapping; otherwise resolution refuses rather than retaining old ancestry.
Native FindClass must use the actual observed ClassLinker, current source
Thread and a real rooted loader Handle. Presence of the named symbol alone
cannot establish this callback's calibration.

Barrier delegates directly to the calibrated FieldStorage source card barrier
on the exact Pool cache root. Final refresh and ownership checks reuse the
actual live Batch roots, immutable DexFile identity and native Class/cache
namespace without JNI, allocations or DEX parsing. `RequiredServices` must be
implemented by the real retained source cleanup and native source commit
owners, proving final source/ClassTable lifetime, reflective/resolution
invalidation, heap replacement, deoptimization and interpreter-cache clearing.
Absent services refuse both Stage and final verification. The adapter cannot
provide these proofs from an opaque readiness boolean.

`Composite` is the cache participant for the complete explicit/implicit graph.
The explicit private incoming cache goes through the calibrated rewrite owner.
An implicit subclass whose source cache is exactly its published old Class cache
is handled read-only. Stage retains the exact stable Batch root slots and DEX
provenance for those records. Prepared validation requires actual linked Class
namespace/cache/parent/index and `VerifyReadonlyPrepared`, which proves source
linkage and unchanged native member mapping before any final Plan exists.
Final validation matches each readonly record to its unique actual old/new Plan
pair and requires `VerifyReadonly` from the formal heap GC-root, native member
cache and public reflection/resolution cleanup owners. Missing Stage/prepared
or final proof refuses. A ClassTable lookup cannot authorize mutation of these
published caches, and Composite never calls the incoming rewrite operation on
them. The core private cache engine remains unchanged.
