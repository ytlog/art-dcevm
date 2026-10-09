# Source-owned hierarchy allocation boundaries

This ARM64 common module consumes the original `Redefiner::ClassRedefinition` allocator, rather than manufacturing mirror Classes or invoking a second calibration clone. Symbol binding alone grants no hierarchy authority.

## Observation and ownership

`Service::ObserveOriginal` observes an unchanged-hierarchy original structural operation. It matches the actual allocator Handle against an independently rooted public original Class using the calibrated named Thread root visitor. Its Pool retains the original Class, selected parent, cache, loader, initial temporary and eventual linked Class. An independent public `Thread.holdsLock` oracle checks the named MonitorEnter/Exit ABI using an exclusively owned ordinary fixture. Original named FindClass must resolve the independently rooted original Class in its actual loader. All these observations are required before graph stage acceptance.

The LoadClass hook calls `Service::LoadClass`: capture happens after the original source SetupClass/SetDexCache and before LoadClass can fail. The EnsureInstanceFieldIds hook calls `Service::EnsureInstanceIds`: capture precedes the fallible IDs allocation. LinkClass may return a resized Class distinct from the initial allocation; both roots are retained and the discarded initial Class must already be retired before successful graph commit. On failure, every captured temporary is retired while the original source DEX still exists. Retirement independently checks namespace/cache, uses original FindClass to prove the temporary is unpublished, takes the actual Class monitor and verifies status `kRetired`. Status 14 is explicitly rejected because fixed source forbids retiring initialized-but-not-visible Classes, even under exclusive mutator ownership.

Incoming C++ allocation failures are caught at `ObserveOriginal` / `Batch::Allocate`. A failed Scope allocation that returns null also records source OOM. The supplied failure callback must bridge to the real source RecordFailure using its actual driver/owner provenance and fixed platform string declaration, without allowing exceptions across the no-exceptions ART source frame. The outer source hook still must catch allocation in Batch construction, public graph Stage and cache preparation, and refund through the original boolean/error boundary. No source cleanup callback may run after DEX destruction.

## Complete graph operation

Construct Batch within the entire original named Redefiner::Run Runnable lifetime. Prepare from the actual allocation-phase Holder (all incoming cache/mirror partitions, including all structural islands); never substitute a final synthetic Plan. The common rooted allocator chooses the closest nested structural owner and resolves requested parents using actual immutable StandardDEX mappings and original FindClass. Before the original allocation call, Authority must grant the exact staged original namespace, selected parent and full immutable incoming declarations. Afterwards, all graph owners check the early captured native linked Class before it can be used for dispatch or verification.

`Prepared::source_old_root` is the stable native address of the real used Pool slot, not a borrowed Class address. Owner/index/schema text alone can collide for same-descriptor classes in different loaders, and the graph deliberately copies declarations. Refresh therefore identifies each record by this slot, checks the complete copied schema and rebinds from live Pool roots without allocating or parsing. Active source Batches/capture frames are thread-local; unrelated global LoadClass/EnsureInstanceFieldIds calls continue through their original trampolines.

A production adapter must provide exact original trampolines for AllocateNewClassObject, ClassLinker::LoadClass, EnsureInstanceFieldIds and FindClass, source RecordFailure bridge and source SetStatusLocked. It must also connect:

1. Ordinary JNI expected graph Stage before original CheckClass; authorize the exact hierarchy comparisons or final/abstract mismatch, while retaining original role/access and CheckRedefinable semantics. This preliminary source marker cannot publish before the mandatory native Plan guards.
2. Whole Run lifetime plus first original FinishNewClassAllocations actual Holder preparation; original final boolean/error and DEX cleanup paths remain authoritative.
3. Source verification resolution against rooted selected parents/replacements and actual private incoming cache preparation before CheckVerification. Published caches of implicit descendants stay read-only until the actual final heap replacement owner applies their reference updates.
4. Actual Collected Plan sealing, root registration/refresh and final BeginCommit after all independent receiver/cache/source services succeed. End the Batch only after successful source commit or completed failure retirement, before source DEX owners unwind.

The module permits guarded final/abstract Class deltas and refuses interface graph changes until a real expanded source cohort covers implementors and arrays. Cache publication/private ownership and readonly final native-cache effects are separate mandatory services. The module is not enabled simply by compiling it, and the host tests do not prove modern-device graph readiness.

## Source audit

The LoadClass declaration `void ClassLinker::LoadClass(Thread*, const DexFile&, const dex::ClassDef&, Handle<mirror::Class>)` was compared in all seven official `android-11.0.0_r1` through `android-17.0.0_r1` ClassLinker headers. AllocateNewClassObject and EnsureInstanceFieldIds are shared fixed declarations in those source implementations. ARM64 compressed roots are uint32 and remain present regardless of CPU-support scope; they do not constitute ARM32 support. Source audit downloads and native test output stay under ignored `build/evidence/`.
