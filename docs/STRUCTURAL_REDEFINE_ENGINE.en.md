# Structural redefinition engine

English | [中文](STRUCTURAL_REDEFINE_ENGINE.md)

This document explains how the enhancement library evolves classes using the structural redefinition transaction built into the device's ART: which decisions it takes over, what each step does, and the semantics of each change. For user-facing semantics, see [LIMITATIONS.en.md](LIMITATIONS.en.md).

## Goals and prerequisites

- **Goal**: provide VM-level DCEVM inside an app process on Android 11–17 / arm64 without root: add and remove fields and methods, change types, signatures, modifiers and inheritance, and retain compatible field values in live objects.
- **Prerequisite**: the app process must be **Java-debuggable from startup** (manifest `debuggable`). In AOSP `openjdkjvmti/art_jvmti.h`, `can_redefine_classes` belongs to `kNonDebuggableUnsupportedCapabilities`. `IsFullJvmtiAvailable()` accepts only `IsJavaDebuggableAtInit()` or forced interpretation; changing the debug state after process startup has no effect.
- **Outside the design**: replacing system ART, relying on root, deriving addresses or offsets from disassembly, or reimplementing ART's instance migration, reference rewriting, deoptimization and cache invalidation.

## Why ART's structural transaction is the engine

Since Android 11, `Redefiner::StructurallyRedefineClasses` (JVMTI extension `com.android.art.class.structurally_redefine_classes`) already implements the hardest parts of DCEVM:

| Step | Existing ART implementation (`ti_redefine.cc`) |
| --- | --- |
| Class allocation | `FinishNewClassAllocations` creates new Class objects for targets and **all loaded subclasses** in topological order. `AllocateNewClassObject` loads from the new DEX and calls `LinkClass`; ClassLinker recalculates the vtable, iftable and field layout |
| Instance migration | `CollectAndCreateNewInstances` walks the heap and allocates a replacement for each old instance |
| Field copying | `UpdateClassStructurally` copies static and instance fields by name and type, migrates the lock word and clears old objects |
| Reference rewriting | `Runtime::VisitReflectiveTargets` rewrites ArtField*/ArtMethod* references in JNI IDs, reflection Field/Method, MethodHandle, VarHandle and DexCache. `HeapExtensions::ReplaceReferences` replaces old objects and classes in the heap and roots |
| Execution state | `DeoptimizeAllThreadFrames` deoptimizes frames, `InvalidateAllCompiledCode` clears JIT code, per-thread interpreter caches are cleared, and old methods are marked obsolete |

ART normally allows additions only; its restrictions are concentrated in a few decision functions. The library takes over those decisions. ART performs the commit, migration and invalidation.

## Interception points

| ART decision point | Original behavior | Enhanced behavior |
| --- | --- | --- |
| `JvmtiFunctions::RedefineClasses` in the public table | Ordinary redefinition rejects structural changes; an exported entry may be inlined | Route to the original structural transaction, retaining argument and capability checks, ClassFileLoadHook and ordinary method-body updates. Before entering ART, load classes referenced by the new DEX through the target's loader, including added interfaces and superclasses, and inspect old declarations |
| `CheckClass` | Class modifiers, superclass and interfaces cannot change | Parse the final DEX, probe the layout, evaluate the [validation rules](#validation-rules) and cache the decision |
| `CheckFields` / `CheckMethods` | Only additions are allowed; modifiers cannot change | Call `RecordNewFieldAdded` / `RecordNewMethodAdded` according to the decision to select ART's structural path; call the original function when no decision exists |
| `AllocateNewClassObject` | Rebuild classes and subclasses using the old hierarchy | On a superclass change, write the new superclass into the caller's superclass handle slot |
| `CheckVerification` | The verifier resolves the class descriptor to the old class | On a superclass change, the current thread's `FindClass` returns the new class object for that descriptor during verification |
| `CollectAndCreateNewInstances` | Allocate replacements for old instances | After the original succeeds, validate the slot declarations and precompute member mappings. Return an error on failure when the transaction depends on custom mapping |
| `CollectNewFieldAndMethodMappings` | A missing method with the same name and signature causes a CHECK, including when copied default/miranda methods disappear during subclass relinking | Write the precomputed mappings; use the original mapping for transactions without a plan |
| `UpdateClassStructurally` | Migrate fields and lock words, mark methods obsolete, replace references, deoptimize and invalidate caches | Establish the visitor's transaction scope and call the original in full. During copying, substitute old field arrays without retired entries, and temporarily point the new class at the old superclass when the superclass changes |
| `Runtime::VisitReflectiveTargets` | Apply original member mappings and shadowing rules | Restore old field arrays; clear retired members' DexCache entries, record retired JNI IDs, and rewrite reflection `Field` and VarHandle references for retired fields. Delegate all other work to the original visitor |
| JNI table, `Method_invoke`, `Constructor_newInstance0`, MethodHandle entries and JNI ID encoding | Cached targets are obsolete methods of the old declaring class; calls may fail receiver checks or use old vtable indexes to dispatch to another method | Throw before dispatch only for retired targets; forward all other calls unchanged |

## Transaction flow

### Installation

`DcevmRuntime.initialize()` loads `libartdcevm.so` and attaches it as a JVMTI agent. `core/agent.cc` calls the idempotent `structural_redefine::Install`:

1. Check that the agent's own jvmtiEnv has `can_redefine_classes` among its potential capabilities; otherwise fail.
2. Call the official `shadowhook_init()` and resolve the seven base symbols by name: the public table's `JvmtiFunctions::RedefineClasses`, the structural entry, three validation points and two `RecordNew*Added` functions. Initialization fails with a diagnostic if any symbol is missing or the named wrapper does not match the public JVMTI table.
3. `method_retirement::Prepare` resolves the names required for mapping, allocation, verification redirection, the visitor and access guards, checks the NDK map layout, and installs JNI wrappers through `SetJNIFunctionTable`. Missing required entries prevent these hooks from being installed and disable member retirement, with a diagnostic. Missing `EncodeFieldId` disables only field retirement; missing both `FindClass` signatures disables only superclass changes.
4. Install hooks in order with `shadowhook_hook_sym_addr()`, installing the standard entry last so routed batches see a complete set of validation points. On any failure, undo installed hooks in reverse order with `shadowhook_unhook()`.
5. Obtain the placeholder fields for retired fields through JVMTI `GetClassFields`, without initializing the classes.

### Standard entry

1. Before entering ART, the thread remains in Native state. For each target, use `Class.forName(name, false, target class loader)` to load every class referenced by the new DEX's `type_ids`, without initialization. For arrays, load the element class; see [class loading during transactions](#class-loading-during-transactions). Keep added interfaces and the new superclass as policy inputs. This runs first because it may load classes. Then inspect old declarations through public JVMTI: methods declared by loaded subclasses, methods in their interfaces that the subclasses do not declare, and public methods of superclasses.
2. Store the batch context in thread-local storage keyed by `jclass`, which is the same value as ART's `ClassRedefinition::klass_`. Create JNI global roots for affected classes and loaded subclasses in advance. An outer recursive transaction lock serializes snapshotting and the ART transaction. A successful nested transaction initiated by ClassFileLoadHook invalidates the outer snapshot.
3. Call `StructurallyRedefineClasses` unchanged. Keep global roots after successful retirement; otherwise release them. Clear the context on return and return ART's result.

ART-internal stages such as `Check*` do not use JNI/JVMTI for loading, traversal, assignability or policy computation. The sole exception is JNI `SetObjectArrayElement` (with named local-reference bridging and exception checks) in the allocation hook, on the current thread's transaction-private arrays and holder. ART 13/16 `ScopedThreadStateChange` does not transition when both states are Runnable. This store executes ART's own GC write barrier, allocates no managed object and has no suspension point. Local-reference capacity is reserved while Native. This exception never applies inside an ART callback, while holding ART locks, or to published arrays.

### Class loading during transactions

ART's structural transaction handles subclasses in two steps:

- `FinishNewClassAllocations` walks the heap, collects resolved subclasses of targets, and rebuilds their class objects under the new classes.
- Only afterwards does `CheckVerification` verify the new DEX.

Between these steps, `ClassDefinitionPauser` prevents other threads from defining classes but allows the transaction's own thread to do so. When the verifier resolves types referenced by new code, it can define an unloaded class through `ClassLinker::FindClass`. A subclass defined this way is absent from the rebuild list and remains linked under the old class after commit:

- Its first instantiation fails verification because `this` is not an instance of the new class.
- If verification has been skipped, its field offsets overlap fields added to the new class.

The standard entry therefore loads every class referenced by the submitted DEX through the target's defining loader before entering ART, without initialization. This includes classes that the verifier might load, so the heap walk sees all subclasses and the policy snapshot (`GetLoadedClasses`) includes them. Types that the loader cannot load are left to the verifier's original behavior. Preloading changes only the timing: it does not execute initializers, and loading failures merely mark the class erroneous, as verification or first use would.

Preloading covers only the bytes submitted to the standard entry. Subclasses can still be missed in two cases:

- A class is first referenced by DEX rewritten by ClassFileLoadHook.
- Another thread loads a subclass before ART pauses class definition.

The engine therefore registers JVMTI `ClassPrepare` on its own jvmtiEnv and enables it only during a transaction, covering all threads. Starting with inspection of loaded classes, preparation of any new subclass of a target marks the attempt. ART creates replacement class objects through `LinkClass`, which does not trigger that event.

A marked attempt fails with `JVMTI_ERROR_INTERNAL` in `CollectAndCreateNewInstances`, ART's last fallible step, without committing the batch. The subclass is now loaded, so the standard entry takes a fresh snapshot and retries, up to three attempts. Each attempt calls ClassFileLoadHook again. `subclassLoadRetries` counts retries in the capability report. Without the method-retirement hooks, this failure point is unavailable and only preloading is performed.

### Interface prototype failure cleanup

The API 33/36 interface prototype accepts multiple interface targets with method-body edits alongside unrelated classes, implementations or their subclasses. Parent/child targets share their highest batch ancestor as owner; disjoint interfaces keep separate groups. Groups sharing a subtype without a batch common ancestor reject with 66 to avoid duplicate migration/commit. Interface replacements use the custom allocation path and unrelated classes use ART allocation, within one atomic ART validation/commit/failure transaction. Holder probes use the actual batch length, the fixed `ClassRedefinition` element declaration, three flag vectors and ART-sorted entry indices. While Native, it reuses the standard entry's `GetLoadedClasses` snapshot to collect prepared implementations, their subclasses and subinterfaces, rejecting proxies with public `Proxy.isProxyClass`; some replacements retain their original superclass. On failure ART frees the new DEX, so a later superclass redefinition could collect these abandoned classes from the heap.

Batch implementations/subclasses allocate from their own incoming DEX and holder DexCache; affected types without an incoming DEX keep private link caches for their original DEX. The first affected `FinishNewClassAllocations` entry completes the entire interface group regardless of ART's superclass-depth order. After probing the full fixed holder, only private `initialized_`, `actually_structural_` and `initial_structural_` flags change: the interface owns the initial structural group; batch implementations are allocated structural dependents and later allocation entries skip them. Like ART's `setup_single_redefinition`, every batch entry retains its new-class slot for DEX verification, but only the interface owns the group class/instance arrays. ART's original instance collection, shared mapping plan and structural update therefore process the group once. Abstract-class admission checks every target against the owner's collected instances, avoiding empty dependent-entry arrays. Existing policy still validates implementation additions, retirement and modifiers. An external implementation superclass may share the batch for body edits; structural edits return 66 because its ART class transaction and the interface transaction would rebuild the same implementation subtree twice. The Native snapshot records overlap, and final DEX policy rejects only when that external superclass requires structural replacement, naming the class and overlap reason. Verification redirection covers the entire private group, matching old classes with named `LookupClass` and loader identity only during current-thread `CheckVerification`; no replacements enter old class tables or old DexCaches.

A named `Redefiner::Run` hook roots every group replacement array in a separate ART HandleScope until the batch result is known. A named `ClassLinker::LinkClass` hook only assigns a linked replacement to a reserved pending HandleScope root, without JNI or heap-array stores. The allocation hook transfers it to the private array after returning outside the callback and ART locks. The pending root covers failures while allocating JNI ID arrays, including classes not yet stored in the holder. Failed returns and native allocation exceptions discard the private replacements before their DEX is freed; successful returns preserve their status.

Cleanup uses the named, allocation-free `Class::SetStatusInternal` writer to set only unpublished replacements to `kErrorUnresolved`, preserving subtype bits. In `android-13.0.0_r1` `mirror/class.h`, `kErrorResolved` still satisfies `IsResolved()` and cannot exclude them from heap scans. The ordinary `SetStatus` initialization, locking, exception and notification protocol does not apply to these discarded private classes. As specified by `class-refvisitor-inl.h`, GC still visits Class instance references and native roots; skipping static references is safe because failure precedes the commit's static-field copy and private replacements never execute initializers.

`InterfaceAbortInstrumentation` exercises real DEX verification failures (an invalid return instruction and calling `String.length()` with an integer receiver, both returning 62), failure after linking, after full allocation, during instance allocation, and from the actual mapping probe in one process. Each failure is followed by superclass and interface redefinition, behavior checks and GC. The incompatible invocation is followed by explicit GC before further transactions. A transforming ClassFileLoadHook also tests a real retry after a late implementation loads. Only the app test module explicitly enables fault injection and test JNI exports through its Debug CMake arguments. It builds a test copy from the same engine source directory. Library-owned Debug and Release builds default to OFF and reject an enabled option in a standalone library build. The test APK selects the app copy. `tools/check-interface-abort-isolation.py` uses `llvm-nm` to check both AARs and their Build-ID-matched unstripped libraries for absent injection symbols, and confirms the APK Build ID belongs to the app test engine.

These behaviors have been verified on Android 13 / API 33 and Android 16 / API 36 in normal and CheckJNI modes; the API 33 control APK with cleanup disabled crashes in the failure test. Prototype limits: actual Java/native memory exhaustion, other ART module revisions, and other Android versions remain unverified. Injected allocation failures do not establish behavior under real memory exhaustion.

Private implementation caches reuse the named `ClassRedefinition::CreateNewDexCache` service with the current driver and a non-owning prefix for the original DEX. The original DexCache is unchanged and the new cache is not registered. ART handles allocation, roots, location, the dex lock and initialization. Descriptor redirection uses the named `LookupClass`, which does not fill the cache (`const char*` on API 33, `string_view` on API 36). Assignability uses JNI `IsAssignableFrom` only in the Native snapshot phase. An `IdentityHashMap` keyed by Class identity distinguishes same-named classes in different loaders. Direct superclass/interface edges feed one Kahn traversal, producing a topology and superclass indices in expected O(loaded classes + affected types + direct edges) work. ART allocation only decodes these roots, checks resolved status and the target-first invariant, and uses the saved superclass index; it performs no class traversal or pairwise assignability ordering. `ClassPrepare` still marks subtypes loaded after capture, rejects before instance allocation and triggers a fresh snapshot/retry at the standard entry. The abort instrumentation covers both a late subclass and a direct late implementation, including successful dispatch after retry and GC. Named `JNIEnvExt::NewLocalRef/DeleteLocalRef` services create real local references, and private array stores use JNI `SetObjectArrayElement`, including ART's write barrier, only outside callbacks and ART locks in the allocation hook. `LinkClass` only assigns the pending root; cleanup covers that root as well as the replacement array.

Thread, Class and HandleScope differences live only in `platform/abi/arm64/androidNN/`. Interface support no longer matches API 33/36 exactly: it reuses `method_retirement::CandidateOrder` release ordering and the same `VerifyTarget` member probes to filter interface declarations. Native capture checks the current JNI environment, OS tid and marking-flag range. Before creating any private HandleScope, admission uses named `Thread::HandleScopeVisitRoots` to observe ART's existing root slots. Its callback only records addresses: no allocation, JNI, object dereference or scope mutation. The single-root scopes in both fixed admission callers must match the candidate header/capacity/size, the actual incoming Handle slot and ART's root walk. A candidate Thread scope pointer is never dereferenced unless it is adjacent to that known root at the source-declared header position.

`Run` requires completed probes; the chosen declaration is local to the transaction thread and restored after nested transactions. Before allocation, holder probes check array length, vector target and begin/end/capacity, all three boolean-vector lengths and the new DEX-cache slot. Only a declaration mismatch disables interface redefinition with an explicit `interfaceRuntime` reason while ordinary class transactions remain available. Only completed probes report `verified`. ART's named Mark service still handles read barriers; the private JNI store exception is unchanged. Device validation remains limited to Android13/API33 and Android16/API36, without claiming support for other platforms or modules. Separate abort-test processes verify Thread, HandleScope and holder rejection, persistent interface disablement and subsequent class transactions.

ART-called hooks and visitors must not propagate C++ exceptions. The interface path no longer registers `VisitClasses` callbacks. Admission, class checking, allocation, and instance/mapping preparation translate failures locally to `OUT_OF_MEMORY` or `INTERNAL` without allocating another diagnostic. The pending root covers replacements whose transfer to the private array fails; failed lookup redirection records an error that allocation must reject. Catching at `Run` alone would already unwind ART frames.

Shared commit mapping, update, reflective visitor, and ID encoding boundaries are explicitly `noexcept`. ART offers no recoverable error protocol after its commit point: native allocation failure there remains fatal, rather than continuing a partial commit. Real memory exhaustion recovery is not established. Removing the visitors also removes injections 6, 7, 9 and 10. Admission, lookup and array-transfer exception injections, plus Native scope-construction failure with restoration of both thread-local values, check error codes, live instances, subsequent parent/interface transactions, and GC on both devices in both JNI modes.

Interface admission reads access flags from the target's current DEX ClassDef with `evolution::ReadClassAccess`, using the same valid interface bits as `method_retirement::VerifyAccess`. JVMTI `GetClassModifiers` cannot supply this baseline: it prefers InnerClass flags, where member interfaces include `static`, while ClassDef and runtime Class flags do not. Nested and top-level interfaces follow the same admission rules.

Permanent disablement is limited to declaration evidence: Thread env/tid/marking fields, HandleScope root traversal under an accepted Class candidate, Class flags checked against their own DEX, the Redefiner's current thread, and holder/private-array/private-DexCache shape and identity probes. Injected Thread/HandleScope/holder probe failures belong to the same category. Missing target member evidence, no supported Class candidate, unavailable ClassDef, initialization changes since capture, incomplete admission, and unsuitable holder index/allocation/structural state reject only the current transaction with a reason. Boolean-vector lengths probe declarations; their values describe the current transaction and are checked separately. Hooks save only static rejection reasons; Native return formats the target descriptor and reason into `lastRejection`. Both the full suite and abort instrumentation update a nested interface's default method on an existing instance, check GC, reject unsupported method addition, and then successfully update a top-level interface in the same process.

The `interface-stress` scenario loads 200 implementations, four subinterfaces forming a diamond, and two affected superclasses. Its snapshot must contain exactly 207 types; it verifies preserved instance fields, old/new dispatch and dispatch after GC. `interfaceSnapshotNanos` measures this attempt's `GetLoadedClasses` and interface capture/ordering. `interfaceAllocationNanos` measures the interface allocation hook, including private caches, linking and array stores. Both use a monotonic clock and nanoseconds, describe the calling transaction thread's latest attempt, and are diagnostics rather than a performance guarantee. Device timings stay in local verification records.

The interface named-service catalog follows the implementation: `VisitClasses` and unused ClassVisitor declarations have been removed. `tools/check-structural-symbols.py` rejects unreferenced interface catalog entries, records catalog/implementation hashes, and reports coverage per name and its declared ELF tables. Admission spellings and array-allocation overloads are alternatives; the catalog total is not a count of names simultaneously required on every device.

ART class states use the complete fixed `ClassStatus` enum shared by ART 13/16 and referenced by the version headers. Status reads return that enum; initialization and cleanup comparisons use named states rather than numbers or JVMTI status masks. Java/DEX access bits use named constants, including interface, class super, synthetic, annotation and enum flags. Proxy detection remains a public Native-phase API check, without internal Proxy flag access.

### Validation

`CheckClass` reads `klass` and `dex_file` according to `redefinition.h`, calling the original function if the class is absent from the context. It then reads the new declaration (ClassDef 0) through `DexView`, probes the layout, reads runtime flags of added interfaces and the new superclass, and calls `structural_policy::Evaluate`:

- On rejection, write only `driver_->result_` and return false; `Redefiner::RecordFailure` itself does the same. Record the reason in `lastRejection` and logcat.
- On acceptance, store the decision, including the retired-declaration list, in thread-local storage keyed by `ClassRedefinition*`. Mark the transaction as mapping-dependent if the decision would be rejected without custom mapping.

`CheckFields` / `CheckMethods` call the appropriate `RecordNew*Added` according to `fields_structural` / `methods_structural`. The decision is cleared after `CheckMethods`.

### Mapping and commit

`CollectAndCreateNewInstances` is ART's last fallible step. After the original succeeds, the engine checks that the slots identify the target's old class, that the replacement uses the supplied DEX, and that the new superclass is correct. It then computes field and method mappings for all affected classes using the selected layout and prepares replacement arrays for retired fields. If any step fails and the transaction depends on custom mapping, return an error without committing the batch; otherwise fall back to ART's original mapping.

`PlanMethods` is shared by every layout and structural transaction, including ordinary classes. It decodes each method's name and full prototype using its declaring class DEX and `dex_method_index`, then matches (name, descriptor); raw indices are not comparable across DEX files. The interface prototype introduced this rule because ART `HasSameNameAndSignature`/`GetCanonicalMethod` consult the obsolete method array's DexCache, whereas copied default/miranda storage belongs to an implementation class. Replacing an interface can invalidate that assumption. The mapping no longer depends on those two named symbols; the catalog and checker retain only currently referenced services.

The remaining commit cannot fail. `CollectNewFieldAndMethodMappings` only writes the plan into ART's maps, after confirming they are empty libc++ trees. `UpdateClassStructurally` calls the original in full. The engine only swaps field arrays and superclass pointers around the copying phase and handles retired members in the visitor. The commit section performs no JNI/JVMTI calls or Java-object allocation.

## Validation rules

`structural_policy` is a pure function that can be tested on the host. Field identity is (name, type, static); method identity is (name, descriptor), and a method's invoke kind is further constrained by [invoke kinds stay fixed](#invoke-kinds-stay-fixed). Before comparison, raw DEX flags are normalized to the JVMTI view, consistently with `ti_method.cc`, `ti_field.cc` and `ti_class.cc`: retain Java flag bits, map a method's declared-synchronized flag to synchronized, and remove native from abstract methods.

“Retirement available” means the method-retirement hooks are installed and this transaction's layout probes pass. Field changes also require field guards.

| Change | Result |
| --- | --- |
| Interface target method-body edits | Accepted with verified interface runtime and member mapping |
| Interface target method addition; deletion or signature change | Reject `METHOD_ADDED` (63) and `METHOD_DELETED` (67), respectively |
| Interface target field changes; method modifiers; parent interfaces; class modifiers | Reject `SCHEMA_CHANGED` (64), `METHOD_MODIFIERS_CHANGED` (71), `HIERARCHY_CHANGED` (66), and `CLASS_MODIFIERS_CHANGED` (70), respectively |
| Unavailable interface runtime capability (unsupported version, named service or probe) | Reject `NOT_AVAILABLE` (98); declaration changes use the standard errors above |
| Add fields or methods | Accept |
| Change existing members' access, final, volatile, transient or synchronized flags, except making an instance method private; Java → native; abstract ↔ concrete | Accept |
| Make a method final when a loaded subclass overrides it | Reject with `METHOD_MODIFIERS_CHANGED` (71) |
| Add an instance method or change its access level when it may displace a loaded subclass's copied default/miranda method | Accept through custom mapping when retirement is available; otherwise reject additions with `METHOD_ADDED` (63) and access changes with 71 |
| native → Java | Accept when retirement is available; the engine handles JNI-entry copying. Otherwise reject with 71 |
| Switch a method between static and instance, make an instance method private, or re-add a retired method as another invoke kind | Reject with 71; see [invoke kinds stay fixed](#invoke-kinds-stay-fixed) |
| Change a method's varargs or strict flags | Reject with 71 |
| Delete methods or change signatures, including native, abstract, constructors and class initializers | Accept when retirement is available; otherwise, or when the declaration is in an interface, reject with `METHOD_DELETED` (67) |
| Delete fields or change their types or static flags | Accept when retirement and field guards are available; otherwise reject with `SCHEMA_CHANGED` (64) |
| Change class visibility, final, abstract, synthetic or InnerClass flags | Accept through the structural path. Adding final with a loaded subclass rejects with `CLASS_MODIFIERS_CHANGED` (70); adding abstract while the class itself has instances rejects with 70 after `CollectAndCreateNewInstances` |
| Switch a class between class, interface, annotation and enum kinds | Reject with 70 |
| Add interfaces, with optional reordering | Accept when retirement is available. Unresolved, non-interface or inaccessible types reject with `FAILS_VERIFICATION` (62); no usable layout for reading flags rejects with `HIERARCHY_CHANGED` (66) |
| Remove an interface; repeat an interface | Removal rejects with 66; see [why narrowing is unsupported](#narrowing-is-unsupported). A duplicate rejects with 62 |
| Change the superclass | Accept when the new superclass extends the original; other cases reject with 62, 61 or 66 under the conditions in [widening superclass changes](#widening-superclass-changes) |

## Runtime probes

Fixed source declarations are selected by probes on each transaction, rather than an SDK, vendor or build-number allowlist:

- **Declaration sources**: compare AOSP source for `android-11.0.0_r1` through `android-17.0.0_r1`, including `12.1.0_r1` for Android 12. Shared declarations live in `platform/abi/arm64/common/`: Object/ObjectArray, the DexCache prefix, ArtField, ArtMethod for 12–17, Handle, RedefinitionDataHolder slots, the reflection visitor and source types, and the libc++ pointer map. Differences live in version directories: the Class field arrays in `android11`–`android17/class.h` (`ifields_/sfields_` in 11–15, `fields_` in 16–17, with `access_flags_` moving accordingly), and `android11/art_method.h`, which has an extra `dex_code_item_offset_`.
- **CheckClass probes**: obtain each target's Class through `ClassRedefinition::GetMirrorClass`, and compare `access_flags_` with ClassDef flags under each candidate declaration. Use the ArtField returned by `FindDeclared*Field` to establish the field-array position, then check each declaring class, JVMTI modifier set and DEX field identity. Check declared method counts, modifiers and DEX method identities with each candidate ArtMethod stride. Candidate arrays are not dereferenced before their position has been established. Consider only versions no older than the platform, since the ART module cannot predate it, and try the platform's own version first. A candidate is usable only if every target in the transaction accepts it. With no usable candidate, the transaction cannot retire members or change copied members.
- **Commit-time checks**: after the first retirement, confirm that ART marked retired methods obsolete; otherwise guards switch to direct table lookup. Obtain MethodHandle's `artFieldOrMethod` offset and the VarHandle access-mask offset from ART's field table through `Class::FindInstanceField`, rather than hardcoding layouts.
- **NDK map**: the pointer-map boundary between ART and the NDK is restricted to libc++ ABI v1. Check NDK type sizes at compile time and verify sentinel, root, size and value layouts at initialization using the library's own empty and one-node maps.
- **DEX identity**: the bounded reader accepts both standard DEX and Android 11–15 CompactDex, where ID tables are in the main region and strings and type lists are in a separate data region. Redefinition input accepts only single-class standard DEX.

## Member mapping and method retirement

Keep ART's `FinishNewClassAllocations`, `AllocateNewClassObject`, `CollectAndCreateNewInstances` and `UpdateClassStructurally`. Every transaction whose probes pass uses custom mapping, including cases where disappearing subclass copied default/miranda methods would CHECK in ART's original mapping. Mapping uses the actual declaring class and members with the same name and signature to distinguish declared, inherited and copied members:

- Map a deleted real declaration to itself and retire it, without fabricating members or offsets.
- Map a disappearing copied method to its real canonical member and clear that DexCache resolution for the transaction. It is not a deleted declaration and does not enter the permanent retirement set.
- A signature change retires the old signature and adds the new one. Restoring a member with the same name and signature creates a new member; old cached targets remain retired.

ART's obsolete marking for all old methods, old declaring-class fixup and ClassExt retention continue unchanged.

### Access guards

Ordinary bytecode calls need no guard. During commit, ART visits every DexCache in the heap, including caches retained by old classes. The visitor clears resolutions to retired methods; a subsequent resolution that cannot find a member throws `NoSuchMethodError`. Deleting an override falls back to a superclass or interface default implementation. The vtable/iftable contains only new methods, and ART clears per-thread interpreter caches and JIT code. Guards are installed only at entries that can still obtain a retired method directly:

- **JNI**: use public JVMTI `SetJNIFunctionTable` to wrap every `Call*Method*` in front of the current table: virtual, nonvirtual and static, including A/V forms, plus `NewObject`/`NewObjectV`/`NewObjectA`. Retired IDs are looked up without decoding and immediately throw `NoSuchMethodError`. Forward other calls to the original table, which is the checked table when CheckJNI is enabled. Two `EncodeMethodId` overloads record index-form IDs encoded after retirement.
- **Reflection**: hook the native implementations of `Method.invoke` and `Constructor.newInstance` (`Method_invoke` and `Constructor_newInstance0`; each runtime has one of two spellings). Identify retired targets before ART checks the receiver or allocates a new-class instance, and throw `InvocationTargetException` wrapping `NoSuchMethodError`. Executing a retired constructor would run old code on a new-class object, so both paths must be intercepted.
- **MethodHandle**: `MethodHandleInvoke`/`MethodHandleInvokeExact` read the handle's `artFieldOrMethod` before dispatch. Combinator handles return to these entries through invoke-polymorphic on Android 11/12. From 13 onward, they call `MethodHandleInvokeExact` through `MethodHandleInvokeExactWithFrame`; guard that function when present to cover compiler inlining.

The deoptimization-resume paths for existing frames are not intercepted. Active old frames continue to execute old method bodies.

### Method kinds

- **native**: ART's `UpdateClassStructurally` looks up every old native method by name in the new class and copies its JNI entry, assuming the destination is still native; this is the same in 11–17. Deletion may yield no method (a null pointer) or a superclass method with the same name. Changing native to Java would overwrite a Java method's data field. Record these old methods during planning and remove their native flags only during ART's copying loop. That loop also writes obsolete stubs into their entries. Because a native method's entry determines active frame sizes during stack walking, restore native flags and original entries at the visitor entry.
- **abstract**: retire abstract declarations like ordinary methods. Calls through the declaration resolve again and throw `NoSuchMethodError`; a subclass's own implementation remains unaffected. When a method becomes abstract, calling it on a subclass instance without an implementation throws `AbstractMethodError`, following JVM linking semantics.
- **Constructors**: retire them like ordinary methods. Bytecode `new` throws `NoSuchMethodError` after resolution; guards reject cached `Constructor` objects and JNI constructor IDs.
- **Class initializers**: an initialized class does not run `<clinit>` again. Deleting it retires the old method while static field values are copied as usual.

### Keeping retired members alive

To prevent GC, class-loader unloading or address reuse from turning retired pointers into dangling keys, the standard entry creates JNI global roots for affected classes while still in Native state. Release them for failed batches or batches without retirement. Successful retirement retains and deduplicates roots across batches. ART's reference replacement updates the roots to the new Class, whose obsolete chain protects old members. **These classes and their class loaders remain alive until process exit.**

## Field retirement

An old field is retired when its name, type or storage no longer appears in the new class. Keep the real old ArtField, without manufacturing a tombstone. A same-name new field in the new class starts at its default value.

- **Copying during commit**: ART's `CopyFields`/`ClearFields` walks old-class field arrays and looks up same-name, same-type fields in the new class; a missing field causes a `CHECK`. After `CollectAndCreateNewInstances`, prepare an array copy without retired entries for each affected old class. Swap the old class's field-array slot to the copy at the start of Update, and restore it when ART reaches the visitor, after copying and before visiting in every version 11–17. Confirm restoration again at the end of Update. After commit, clear retired static reference fields in the old class so permanently retained old classes do not keep those objects alive.
- **Bytecode**: the visitor clears retired-field resolutions from DexCache. Resolving `iget/sget` again throws `NoSuchFieldError`; old active frames also fail when accessing retired fields.
- **JNI**: wrap `Get/Set<Type>Field`, `GetStatic/SetStatic<Type>Field` and `ToReflectedField` in the JNI table. The visitor records retired IDs in the JNI tables. `JniIdManager::EncodeFieldId` records retired or placeholder field IDs encoded afterwards; there are two overloads for each of the old and new signatures, and either complete pair suffices. Pointer-form IDs are compared by address. Matches throw `NoSuchFieldError` without decoding the ID.
- **Reflection `Field`**: `ReplaceReferences` changes a `Field` object's declaring class to the new class while its index and offset still describe the old field. The visitor maps it to a field of the same primitive type in the library's placeholder class, `com.github.ytlog.artdcevm.internal.RetiredField` for instance fields or `RetiredStaticField` for static fields. `Field::VisitTarget` then rewrites the declaring class, index and offset. Instance access fails the receiver check with `IllegalArgumentException`. `RetiredStaticField` initialization has already failed during installation, so static access throws `NoClassDefFoundError`, caused by `NoSuchFieldError`.
- **MethodHandle**: the handle guard reads `artFieldOrMethod` and throws `NoSuchFieldError` when the target is a retired field.
- **VarHandle**: accessors in 11–17 first check `accessModesBitMask`, both in `VarHandleInvokeAccessorImpl` and the JIT intrinsic. The visitor zeros the mask of a VarHandle targeting a retired field, so later access throws `UnsupportedOperationException`.
- **Uncovered access**: raw field offsets cached by `sun.misc.Unsafe`, `Atomic*FieldUpdater` and similar APIs bypass all ART entry points. Any structural redefinition that changes an instance layout, including adding fields, invalidates them. This is a restriction of ART's structural redefinition itself.

## Adding interfaces

ART's `AllocateNewClassObject` does not call `LoadSuperAndInterfaces`. It marks the new class kLoaded and calls `LinkClass` directly. For each interface in the new DEX, `LinkClass` calls `ClassLinker::LookupResolvedType` ([16: NonProxyInterfacesAccessor](https://android.googlesource.com/platform/art/+/refs/tags/android-16.0.0_r1/runtime/class_linker.cc#7183); Android 11 uses [`Class::GetDirectInterface`](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/runtime/mirror/class.cc#1217)), looking only in the new class loader's class table. A miss returns null, which release builds dereference. A hit is checked only for being an interface, not for accessibility. The engine therefore handles:

- **Pre-resolution**: before entering ART, call `Class.forName(name, false, target's defining loader)` for every added interface. In 11–17, `Class.forName` calls `ClassLinker::FindClass`, which inserts the returned class into the initiating loader's class table even when a parent loader defined it ([16: class_linker.cc#3444](https://android.googlesource.com/platform/art/+/refs/tags/android-16.0.0_r1/runtime/class_linker.cc#3444)). `LinkClass` can then find it. Interfaces are not initialized here. `CheckClass` reads DEX after ClassFileLoadHook; the policy rejects unresolved interfaces introduced there.
- **Reading flags**: `CheckClass` decodes each resolved interface with `GetMirrorClass`, which in 11–17 reads only `driver_->self_` and `klass_`. Read Java bits from `Class::access_flags_` using the layout accepted by this transaction's probes. These must be ClassDef flags: ART's `Class::CanAccess` reads them, while JVMTI `GetClassModifiers` reports InnerClass flags. For example, a protected nested interface has public ClassDef flags.
- **Policy**: reject with 62 if a type cannot be resolved, is not an interface, or is neither public nor in the same runtime package, which requires both the same package name and defining loader. Reject with 66 if no usable layout is available to read flags.
- **Mapping and subclasses**: adding interfaces only adds copied default/miranda methods or turns an old copy into a conflict method; it does not make old copies disappear. `FinishNewClassAllocations` relinks loaded subclasses, which also acquire the new interfaces.

## Widening superclass changes

The new superclass B must be a proper subclass of the original superclass A, for example by inserting a class between T and A. T retains every existing supertype, so verified code's type assumptions remain valid. Instances of T and its loaded subclasses acquire B's fields, initialized to zero, and its methods. ART's transaction assumes an unchanged hierarchy, so four decisions need interception:

- **Resolution and policy**: resolve B before entering ART, without initialization. Use JNI to check whether B extends A or instead extends T, and JVMTI to collect final methods declared between B and A. `CheckClass` reads B's runtime ClassDef flags through the selected layout. Reject when:
  - B cannot be resolved, is an interface, is final, or is inaccessible: 62;
  - B extends T, creating a cycle: `CIRCULAR_CLASS_DEFINITION` (61);
  - B does not extend A, which narrows the hierarchy: 66;
  - the new definition or a loaded subclass overrides a final method in B's chain: 62;
  - the batch also includes a superclass or subclass of T, or a class in B's chain: 66;
  - custom member mapping or verification redirection is unavailable: 66. B may make copied default/miranda methods in T disappear, which requires custom mapping.
- **Allocation**: `AllocateNewClassObject` puts the old superclass into a slot in the caller's handle scope, then calls `LinkClass`. The hook writes B into this slot, which remains a GC root, so the new class links under B. Loaded subclasses are then rebuilt under the new T. After `CollectAndCreateNewInstances`, confirm that the new superclass is B; otherwise reject with 66.
- **Verification**: starting with Android 13, the verifier resolves the class itself by descriptor through its loader and still obtains the old class, whose superclass is A. A new constructor calling B's constructor would fail verification. ART on Android 11/12 itself writes the new class into the new DexCache. The engine hooks `ClassRedefinition::CheckVerification`: while verifying this class, the current thread's `ClassLinker::FindClass` returns the new class object from the holder for that descriptor. There are two signatures and every runtime has at least one. If another verifier uses a different resolution path, verification fails with 62; unverified code is never committed.
- **Field copying**: ART's `CopyFields` pairs old and new superclass chains level by level; differing chain lengths cause a `CHECK`. Because B extends A, field offsets in A's chain in the new T remain the same as in the old T. At the start of Update, temporarily change the new T's superclass pointer back to A. ART then pairs (T', T), (A, A), and subsequent levels, copying T's own fields and all inherited fields. Restore B at the visitor entry and at the end of Update.

B is not initialized during redefinition; its static initialization runs when first needed. Existing instances do not execute B's constructor.

## Narrowing is unsupported

Any loss of a supertype, whether removing an interface or omitting an old superclass from the new chain, is rejected with 66. The issue is verified code, not just existing references. ART's verifier previously proved that a T value could be used as L, so this code has no runtime cast.

- **Interfaces**: after narrowing, JIT compilation of code such as `L l = t; l.m();` dispatches through an IMT slot. A single-method slot stores the implementation directly (`ClassLinker::SetIMTRef`), without checking that the receiver implements L, and may jump to another method of T.
- **Superclasses**: the interpreter does not check the receiver type for access already resolved to a field in superclass A, either. It reads or writes the T instance using A's offset.

Checking the heap and stacks at commit covers only existing references, not this code when it executes later. Safe narrowing would require ART to reverify every loaded class that may depend on the old type relationships. No usable named entry exists for that operation.

## Invoke kinds stay fixed

Verified code resolves a method by name and signature without checking that it still has the invoke kind it was verified against. Methods that skip access checks use unchecked resolution, and from Android 16 that path (`ClassLinker::ResolveMethodId`) no longer receives the invoke type. The instruction then invokes the result as its own invoke type:

- `invoke-static` that resolves to an instance method runs the instance body without a receiver argument.
- `invoke-virtual` that resolves to a static or private method dispatches through a vtable index the method does not own, reaching another method.

Retiring the old method clears the callers' DexCache entries, so the next call takes exactly this path. The engine cannot make it throw `IncompatibleClassChangeError`. A method identity (name, descriptor) therefore stays static or instance, and a virtual method never becomes private. A private method may become virtual: only the class's own code invokes it directly, and that remains valid. Constructors and class initializers are always invoked directly and are not restricted.

The engine records the methods each class retired in committed transactions, with their modifiers, and checks a later re-addition of the same identity by the same rule. The record lasts as long as the process. Field resolution includes the static flag and cannot find a field with the other storage, so a field's static change can retire like any other.

## Active frames

In a process debuggable from startup, app code is either JIT-compiled in debuggable mode or interpreted. ART's `DeoptimizeAllThreadFrames` covers all app frames. Only boot-image frames cannot be deoptimized. Boot code is compiled independently, does not inline app code and holds only references. Member changes therefore need no additional handling: ART's deoptimization and obsolete-method mechanisms provide it.

## Capability report

The JSON returned by `DcevmRuntime.capabilities()` contains:

| Key | Meaning |
| --- | --- |
| `installed`, `standardRedefineRouted` | Base entries are installed, and standard `RedefineClasses` is routed to the structural transaction |
| `memberAddition`, `memberModifierChange`, `classModifierChange` | Member additions and member/class modifier changes; true after installation |
| `methodDeletion`, `methodSignatureChange`, `interfaceAddition` | Method-retirement hooks are installed |
| `fieldDeletion`, `fieldTypeChange`, `memberDeletion`, `memberTypeChange` | Field guards are also available |
| `superclassWidening` | Method-retirement hooks and verification redirection are both installed |
| `hierarchyChange`, `fullDcevmComplete` | Always false: narrowing is unsupported and full DCEVM is not complete |
| `memberMapping` | Latest probe result: `unavailable`, `unverified`, `verified: androidNN class layout`, or `rejected: reason` |
| `jniRetirementGuards` | Count of actual JNI/reflection guard rejections of retired targets |
| `subclassLoadRetries` | Count of retries caused by a subclass of a target loading during a transaction; see [class loading during transactions](#class-loading-during-transactions) |
| `diagnostic`, `lastRejection` | Installation diagnostics; the classes and reason of the latest standard-entry transaction failure, empty on success. Rejections by the policy, the snapshots and the pre-commit checks state their own reason. When one of ART's own checks rejects the batch (an interface, for example, is rejected with 79 at its entry), the standard entry names the rejected interface or unmodifiable class, or otherwise lists the batch's classes and the error name; ART's own message appears only in logcat after `FAILURE TO REDEFINE` |

Whether a capability is usable still depends on each transaction's probes. Failed probes reject the corresponding changes with JVMTI errors.

## Named symbols

Private ART capabilities are accessed only through named ELF symbols. The official ShadowHook resolves them at runtime within the table scope declared in the catalog. `.gnu_debugdata` is also a named symbol table; no disassembly or address derivation is involved.

- [structural_redefine_symbols.h](../artdcevm/src/main/cpp/platform/symbols/structural_redefine_symbols.h): 22 currently used names provided by every runtime. Of these, 19 require `.dynsym`; the standard JVMTI wrapper and two MethodHandle entries allow any named table.
- [method_retirement_symbols.h](../artdcevm/src/main/cpp/platform/symbols/method_retirement_symbols.h): names whose spellings vary across builds, tried in order. `Method_invoke` and `Constructor_newInstance0` each have a spelling without a suffix and one with a fixed `.__uniq` suffix. `EncodeFieldId` and `FindClass` each have old and new signatures. `MethodHandleInvokeExactWithFrame` is optional, from Android 13 onward.

A symbol's presence proves only that it can be located. Runtime probes check the fixed declarations; device regression checks hook coverage.

## Source organization

Under `artdcevm/src/main/cpp`:

| Component | Location |
| --- | --- |
| JVMTI agent entry and JNI exports for `DcevmRuntime` | `core/agent.cc` |
| Engine: prerequisites, hook installation, standard-entry routing, validation hooks and capability reporting | `platform/common/structural_redefine_engine.{h,cc}` |
| Validation policy, a pure function testable on the host | `platform/common/structural_policy.{h,cc}` |
| Member mapping, retirement, access guards, runtime probes and candidate layouts | `platform/common/method_retirement.{h,cc}` |
| Old-schema inspection through public JVMTI | `platform/common/schema_snapshot.{h,cc}` |
| Class declarations read from ART's mapped DEX | `platform/common/art_dex_view.h`, `evolution/class_schema.h`, `evolution/dex_schema.cc` |
| Fixed declarations shared by 11–17 | `platform/abi/arm64/common/` |
| Declarations with real version differences | `platform/abi/arm64/android11`–`android17/` |
| Named symbols and ShadowHook lookup | `platform/symbols/` |

Reflection placeholder classes for retired fields live in `artdcevm/src/main/java/com/github/ytlog/artdcevm/internal/`.

## Risks

- **Mainline updates**: ART APEX is replaced through Google Play system updates, so the platform version can differ from the ART source version. Candidate declarations cover source versions 11–17 and are checked by runtime probes on each transaction. New ART source versions require source review; add a version directory when there is a real difference.
- **ART service changes**: probes can verify only declarations with ART services or JVMTI as a reference: Class, ArtField, ArtMethod, DexCache, slots, maps, obsolete flags and MethodHandle offsets. Visitor vtable order, source types, Handle and guard signatures rely on source declarations and exact symbol names. Check source and device behavior when extending version coverage. Put differences only in `platform/abi/arm64/androidNN/`, without copying the backend.
- **Retired access boundaries**: retain real old ArtMethod/ArtField objects and guard new access. Raw offsets cached by `Unsafe` and field updaters cannot be intercepted.
