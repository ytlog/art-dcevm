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
