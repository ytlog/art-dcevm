# Common staged hierarchy graph authority

`platform/common/native_hierarchy_graph.{h,cc}` supplies an operation-scoped
preallocation authority shared by source-backed Android providers. It does not
install an allocator, receiver check, type-cache hook, or original `CheckClass`
hook. The caller must supply independently proved `AllocationOwner`,
`ReceiverOwner`, and `CacheOwner` services; absent owners reject before a staged
hierarchy grant is published. Named symbols or a supported SDK do not prove an
owner.

## Public stage and identity

`Stage` runs inside the existing ordinary Runnable JNI callback. JVMTI supplies
the complete loaded ordinary-Class snapshot and independently observed
signatures, class modifiers and initialization status. Public
`Class.getSuperclass`, `getInterfaces`, and `getClassLoader` establish edges and
loader identities. Proposed types resolve through
`Class.forName(name, false, actualLoader)` and must already occur in the snapshot.
If lookup or interface resolution loads another Class, this attempt refuses;
the caller may restage a new complete graph before invoking source redefinition.
Arrays and primitives are omitted, erroneous ordinary Classes reject the stage.

The authority owns Class and loader strong globals until `Clear` after source
Run and its cleanup finish. It copies every incoming schema. JNI root handles
remain authoritative across GC: native Class/loader pointers are freshly decoded
through the privately owned, named no-JNI decoder on `CheckClass`,
`AuthorizeAllocation`, and `VerifyPrepared`. The original public decoder that
calls `GetObjectRefType` is not suitable for these no-suspend comparisons.
The source allocation owner reloads unpublished replacement Classes, incoming
caches and selected parents from its actual source HandleScope pool using
`RefreshAllocations`. Descriptor equality never substitutes for Class or loader
identity.

The graph detects cycles across batch and unchanged edges, duplicate interfaces,
wrong parent/interface roles, final parents, loss of bootstrap Object ancestry,
and inaccessible types (public, or exact loader plus package). Added parents
must have independently initialized before Stage. The affected cohort includes
implicit subclasses and implementors through current and proposed reverse
edges. Ordinary target class role/access must remain identical; final/abstract changes have independently stored proposed flags and mandatory later native Plan guards. Interface-target
and bootstrap-Object evolution still refuse: no independent owner for these
source semantics is supplied here. Existing source role/access checks and
`CheckRedefinable` remain required.

## Source owner protocol

1. Stage the public graph and prove all three installed runtime owners.
2. The original `CheckClass` hook may use `CheckClass` for its hierarchy comparisons or exact
   final/abstract mismatch and only with its exact rooted original Class and exact
   immutable incoming declarations.
3. Reserve source GC-updated old/new/cache/parent slots before allocation.
   `AuthorizeAllocation` receives `Prepared.new_class == nullptr`; the incoming
   cache's actual immutable DEX declaration/index, native ClassRedefinition
   owner, proposed parent and receiver/type-cache capability are independently
   checked by all owners. Affected parents must have linked first.
4. At the actual source early linked-Class/EnsureInstanceFieldIds boundary,
   capture the new Class before a subsequent IDs allocation can fail.
   `VerifyPrepared` checks native namespace, selected parent and complete native
   IfTable identities against the staged graph. The allocation owner verifies
   actual incoming cache and source owner; the supplied declaration pointer is
   not sufficient evidence. Preserve temporary roots and retire failed linked
   Classes before their source DEX owner disappears.
5. `Seal` requires the actual Plan to contain every affected Class exactly once,
   paired with its independently accepted linked replacement. Root registration
   then freezes Class/loader/replacement identities, IfTable ordering witnesses,
   allocation caches/parents, Plan classes/instances/mirror/Holder arrays and
   partition origins.
6. Final `CanRefresh`/`Refresh` and `Verify` use existing storage only. Verification
   checks the same Plan object, pair ordering, exact roots, original/proposed
   native superclass, fixed source Class flags and native IfTable identities,
   plus every installed source owner. No JNI, new graph parsing or DEX parsing
   occurs. `BeginCommit` belongs only after all owners' rejectable checks pass.

`native_interface_graph` performs the actual source IfTable reads. The authority
checks their complete identities as sets against public graph closure while
retaining actual source order for allocation-free final `Matches` checks.
`modern_field_metadata` remains the independently calibrated native Class
identity provider; the fixed source Class state reader checks flags.

## Validation and bounds

`python3 tools/test-native-hierarchy-graph-host.py` exercises cycle, loader/access,
initialization, role/finality and implicit-cohort decisions, a 4096-deep graph,
and the authority lifecycle through fake public JNI/JVMTI calls. It moves every
public native identity between Stage and source authorization and moves old/new
Classes and interfaces again between Seal and root refresh. Wrong parent,
wrong cache/index, changed declarations, missing owners, and expired owner
proofs refuse. Final refresh and checks execute with C++ `operator new` aborted.

Host metadata/interface leaves and runtime owners are explicitly fixtures. This
proves authority logic and its owned-storage behavior; it does not prove actual
ART root decoding, allocator interception, failed-allocation retirement,
receiver dispatch coverage, type-cache update, or full hierarchy evolution on
any device. ARM64 Android API30 NDK syntax verification checks source compilation.
No Android/vendor/BuildID whitelist exists in this authority.
