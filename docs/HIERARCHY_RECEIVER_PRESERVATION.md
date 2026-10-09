# Additive hierarchy receiver proof

`native_hierarchy_receiver::Preservation` implements the typed graph authority
`ReceiverOwner` for an additive path. Construct it with the actual calibrated
`modern_field_metadata::FieldMetadata`, supply it as the staged graph's receiver
binding, and call `Reset()` between whole source operations after graph cleanup.
It installs no receiver hooks and grants no narrowing.

The public staged graph must preserve every previously valid Class/interface
assignability relation of every affected node. This is a complete ancestry and
transitive-interface check across all batch ancestors; comparing only each
changed class's immediate parent/interfaces is insufficient. Inserting a new
parent below the old parent, or replacing a direct interface with an interface
that transitively extends it, can preserve the old relations. Removing an old
ancestor/interface, including through another batch definition, refuses. Equal
descriptors in different loader identities remain different relations.

The proof independently derives the implicit affected cohort from current and
proposed relation reachability to the exact targets. An omitted subclass or
implementor refuses. It binds the exact graph object, strong Class/loader handles,
descriptors, class flags, original/proposed edge indices, initialization witnesses
and affected bits. Target declarations must match the proposed hierarchy and
retain their role/modifiers. Primitive/array/proxy declarations and unproved
Object/interface superclass roles refuse. Existing member/access/abstract/final
schema policies continue to require their own actual owners.

The owner independently matches original native metadata and complete IfTable
identities to the public graph. Before allocation, it checks exact selected
parent, immutable proposed declaration hierarchy and rooted source graph. At
linked allocation it checks the actual new Class namespace, role, parent and
complete native interface relations. Native IfTable ordering is retained as a
witness; this owner does not prescribe one public traversal order for ART.

No independently managed native root is retained. Graph authority owns the
actual public globals and source early-allocation roots. Immutable node indices
rebuild existing native expected slots after every actual source GC refresh.
After linked preparation, final verification checks the exact same source Plan
object and pair ordering against the refreshed graph, along with every original
and replacement native relation. All relation/order storage exists before the
final source stop: verification invokes no JNI, parses no DEX and allocates no
C++ storage. `BeginCommit` has no receiver mutation for this preservation path.

A receiver grant is only one part of hierarchy readiness. Genuine allocation,
failed-Class retirement, type-cache reference updates, deoptimization and source
member/dispatch services are still required. If any old relation is lost, this
module refuses; a narrowing owner must actually cover interpreter, compiled/AOT,
JNI, reflection, MethodHandle and VarHandle receiver paths. No such narrowing
owner is implemented or claimed here.

## Evidence

`python3 tools/test-native-hierarchy-receiver-host.py` runs ASan/UBSan fixtures
with real common preservation logic. Positive evidence includes inserting a
parent below the old parent, adding inherited/transitive interface relations,
nontrivial actual IfTable order, implicit descendants, and two rounds of moving
Class/parent/interface/replacement identities. Final proof runs with C++
`operator new` aborted. Wrong linked relations, stale raw Class pointers,
changed pending caller facts, a different Plan or wrong pair, removed ancestors,
changed batch superclass/interface ancestors, loader identity substitution,
modifier/role/array/proxy errors and missing implicit cohort refuse.

Host metadata/IfTable/calibration leaves are explicit fixtures reused from the
graph test. These tests establish the preservation logic, frozen storage and
moving-root contract, not live ART receiver/type-cache/deoptimization behavior.
ARM64 Android API30 NDK `-Werror` syntax checks pass. No device or APK was used.
