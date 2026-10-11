# Runtime helper continuation episodes

This is an implementation design, not a completed runtime permission proof.

## Current strict boundary and future capability

`whole_process_frames.h::AuthorizePending` now explicitly rejects a runtime
boundary encountered while `escape` is already true, with
`whole-continuation-runtime-native-return-owner-unproved`. It does not carry that
unproved older helper through to terminal authorization. The existing terminal
contract separately requires the bottom original invoke stub, all actual fragments,
transaction roots and installed native JNI member guards. Those guards cover
JNI/member entrypoints; they do not establish the semantics of an intermediate ART
C++ quick helper's continuation after a managed callee returns. The episode design
below proposes a future closed capability to discharge that specific obligation;
it is not an implemented permission to bypass the strict rejection.

The additional exception-continuation obligation starts only when `escape` is
already true while walking from younger to older frames. A runtime frame visited
before the first escaping AOT/managed frame does not acquire this obligation. In
particular, `runtime -> AOT main -> transition` does not justify rejecting the
runtime frame for an exception originating in that older AOT continuation.
This design does not replace existing normal-return, map, root or deoptimization
obligations.

A runtime ArtMethod sentinel, null owner/flags/entry, matched frame geometry and
original successor prove a callee-save frame. They do not identify its C++ helper.
Fixed Android 16 ARM64 assembly uses the same save-frame families for distinct
operations:

- `INVOKE_TRAMPOLINE_BODY`, lines 381–398: create SaveRefsAndArgs, call the original
  C++ resolver, restore, then tail-call the selected code or deliver an exception.
- `ONE_ARG_SAVE_EVERYTHING_DOWNCALL`, lines 1190–1206: call an indexed-DEX C++
  helper, then return/deoptimize or deliver the pending exception.
- Lines 1230–1239 instantiate class initialization, type resolution, method-handle,
  method-type and string resolution, then generate field entrypoints.

Source: [fixed Android 16 quick entrypoints](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/arch/arm64/quick_entrypoints_arm64.S).
The verified local copy is
`build/evidence/compatibility/20261008/deoptimize-if-needed/android-16.0.0_r1/runtime/arch/arm64/quick_entrypoints_arm64.S`.
`StackVisitor::GetCurrentQuickFrameInfo`, lines 759–798, intentionally returns
canonical runtime frame geometry, not helper identity:
[fixed Android 16 stack visitor](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/stack.cc).

## Proposed hook and provenance implementation

1. Select an actual named ELF C++ quick helper and use its verified fixed Android
   16 C++ declaration/body from the inventory below before writing its typed
   trampoline. The source is now available; actual runtime hook coverage and its
   closed continuation contract are still unproved. Candidates established by the
   assembly include `artInitializeStaticStorageFromCode`,
   `artResolveTypeFromCode`, `artResolveTypeAndVerifyAccessFromCode`,
   `artResolveMethodHandleFromCode`, `artResolveMethodTypeFromCode`,
   `artResolveStringFromCode`, and the actual resolver targets instantiated by
   `INVOKE_TRAMPOLINE`. Symbol absence does not justify replacing these hooks with
   a frame-size classifier. Do not infer a C++ ABI solely from assembly comments.
2. At the real original helper hook ingress, bind its actual `Thread*` argument,
   original named helper identity and unique nested episode. Read the actual
   current fragment and quick head using the already independently calibrated
   Thread/ManagedStack source. Verify current thread and original Runnable/shared
   mutator contract from this exact helper declaration. Record the live runtime
   placeholder, quick SP and fragment token. An expected chain pointer supplied by
   the scanner cannot create this episode.
3. Observe the existing named `ArtMethod::Invoke` ingress inside that episode.
   Bind the actual `self`, method, parent fragment and pushed invocation fragment
   at a genuinely observed original boundary; use the actual managed walker/dispatch
   proof to match the callee, including every intervening logical frame. The named
   `ArtMethod::Invoke` ingress precedes its PushManagedStackFragment operation;
   observing that ingress alone does not reveal the subsequently pushed fragment.
   An actual after-push/managed-entry observation or independently reconstructed
   original complete chain is still required, otherwise the episode is unproved. The helper name or
   SP alone cannot prove that a particular managed callee belongs to the episode.
4. Maintain bounded per-thread nested records prepared before source-stop work.
   Stopped readers consume only the same actual stopped thread's published active
   episode; no JNI, allocation or guessed native stack unwinding during the scan.
   Threads without pre-existing records and already-entered helpers without an
   observed original ingress are unproved. Do not retroactively synthesize records
   from a runtime placeholder. Nested/reentrant helpers require separate records.
5. On an older runtime boundary with `escape == true`, require exact helper-episode
   identity: actual thread, runtime SP, independently observed fragment, original
   managed callee and the corresponding source-stop/transaction epoch. Preserve
   the obligation until its helper-specific continuation contract is proven.

The hook wrapper's normal return is not a universal lifetime boundary. Helpers
that long-jump or deliver exceptions require a separately observed original exit
or a fixed source nonreturning contract; C++ destructors and a stale TLS flag
cannot stand in for that proof.

## Proposed helper-specific retirement contracts

Each covered helper must have a concrete fixed-source dependency description:

- Enumerate native cached `ArtMethod*`, `ArtField*`, entrypoints, dex indices,
  declaring classes, receiver/value roots and offset/type decisions that remain
  live across the observed managed call.
- Check every dependency against the same actual retirement Plan. Stable native
  pointers or relocated object roots alone do not prove that a cached old field
  offset or selected code entry is usable after retirement.
- If fixed source accesses a member after the managed return, either demonstrate
  its unchanged same-Plan dependency closure, or observe the actual post-return
  named re-resolution/guard boundary before the original access. Installing a
  public JNI field guard does not intercept `artGet*FromCode`/`artSet*FromCode` or a
  direct C++ `ArtField` access.
- Root values must be re-read from their actual surviving Handles/source roots
  after relocation. A previously borrowed raw managed pointer is not a receipt.
- A source proof that a particular helper returns null on an escaping managed
  exception and immediately delivers it may discharge only that helper's
  exceptional path, after its actual pending exception and episode are observed.
  It says nothing about its normal return or about a different helper with the
  same callee-save geometry.

A closed helper contract should be a typed source owner consuming the immutable
same-Plan dependencies and actual episode, not a supplied `native_safe` boolean.
If no contract exists, fail specifically for the older helper episode rather than
rejecting an unrelated younger runtime frame.

## Next interface for the observed refs-and-arguments boundary

The actual Android 16 run `7f00dec53df74d99a57890177059a0e7` reported an
immediately younger runtime callee-save frame with PC zero and `hasYounger=false`
at the authenticated caller invoke. The next named diagnostic classified its
method as the internal callee-save reference-and-argument-registers method.
That identifies a frame family, not the original helper, dispatch target or
entered managed body. The observed addresses are diagnostics, never grants.

The next proposed common module is `native_runtime_helper_episode::Service`.
The following is an interface design, **not implemented API or support**:

```cpp
class Service final {
 public:
  // Prepare exact named hooks and bounded per-thread storage in ordinary JNI.
  static std::unique_ptr<Service> Prepare(const SourceBindings&, std::string*);
  // Exercise the owned fixture, actual hook ingress/exit, moving roots and push.
  bool Calibrate(JNIEnv*, const OwnedFixture&, std::string*);
  // ActualRuntimeFrame is constructible only by the original scanner owner.
  bool ReadStopped(const native_source_stop::Session&, void* stopped_owner_self,
                   const ActualRuntimeFrame&, Receipt*, const char**) const noexcept;
  // Typed helper-specific Plan/roots/native dependencies, never native_safe bool.
  bool VerifyContinuation(const Receipt&, const SamePlanDependencies&,
                          const char**) const noexcept;
 private:
  // Typed wrappers alone create Ingress/Returned/ManagedEntered episode records.
  // Public callers cannot manufacture a Receipt or supply a safety callback.
};
```

`SourceBindings` must retain independently calibrated source roots, native method
identity, thread-state/ManagedStack readers, original hook pointers and the exact
VM. Preallocation must occur before source Run; stopped reads allocate nothing.
The service needs a new closed original-helper ingress adapter: current source
readers cannot be called with a fabricated Runnable context merely because an
input `Thread*` or map is readable. Only the typed named wrapper's actual original
arguments and fixed shared-mutator contract may establish that ingress.

Start with these fixed16 wrappers from
[quick_trampoline_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc):

| Helper | Actual original inputs / return | Required episode fact |
| --- | --- | --- |
| `artInvokeInterfaceTrampoline`, 2288–2404 | `ArtMethod* interface_method, Object* receiver, Thread* self, ArtMethod** sp` / `TwoWordReturn` | Actual interface input may be null/runtime; selected target is not known until original return. |
| `artInvokeInterfaceTrampolineWithAccessCheck`, 2254–2258 | `uint32_t method_idx, Object* receiver, Thread* self, ArtMethod** sp` / `TwoWordReturn` | Bind actual caller DEX/index, receiver and resolved target separately. |
| `artQuickResolutionTrampoline`, 1203–1390 | `ArtMethod* called, Object* receiver, Thread* self, ArtMethod** sp` / `const void*` | Record actual input and original return code plus re-read `*sp`; original writes selected method into it. |
| `artQuickToInterpreterBridge`, 761–850 | `ArtMethod* method, Thread* self, ArtMethod** sp` / `uint64_t` | Input method differs from runtime placeholder; track original ShadowFrame and actual push/entry/return lifetime. |
| `artQuickProxyInvokeHandler`, 899–980 | `ArtMethod* proxy_method, Object* receiver, Thread* self, ArtMethod** sp` / `uint64_t` | Actual slot is proxy method; original retains JNI locals/argument vector and invokes its handler. |
| `artQuickGenericJniTrampoline`, 2046–2145 | `Thread* self, ArtMethod** managed_sp, uintptr_t* reserved_area` / `const void*` | Actual native method is initially `*managed_sp`; original rebuilds JNI frame and may change thread state. |
| `artQuickGenericJniEndTrampoline`, 2162–2175 | `Thread* self, jvalue result, uint64_t result_f` / `uint64_t` | Starts without shared mutator ownership; no managed borrow/root publication before the actual Runnable transition. |
| `artInvokePolymorphic` / `artInvokePolymorphicWithHiddenReceiver`, 2408 / 2531 | `Object* receiver, Thread* self, ArtMethod** sp` / `uint64_t` | Separate MethodHandle/ShadowFrame/call-site continuation contract. |
| `artInvokeCustom`, 2632 | `uint32_t call_site_idx, Thread* self, ArtMethod** sp` / `uint64_t` | No instance receiver; bootstrap/call-site dependencies remain separate. |

The retained muyu ELF inventory establishes the Interface, access-check,
resolution and interpreter helper names. The other candidates require their own
explicit name-inventory checks; every wrapper requires actual event calibration. Do not infer absent/inlined function addresses. Additional
`artTestSuspendFromCode(Thread*)` / `artImplicitSuspendFromCode(Thread*)` wrappers
are declared in fixed16
[quick_thread_entrypoints.cc:42–74](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_thread_entrypoints.cc).
Their actual current quick head must come from the independently observed source
ManagedStack, since no SP argument exists; they do not prove interface dispatch.

The exact ARM64 `TwoWordReturn` declaration in
[libartbase/arch/instruction_set.h:324–341](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/libartbase/arch/instruction_set.h)
is `{uintptr_t lo, uintptr_t hi}`. Successful interface return has
`lo = selected ArtMethod`, `hi = selected code`. Failure initializes only `lo=0`;
**never read or record `hi` on failure**. Source-contract failure handling must
preserve the actual exception, not clear/rethrow it to create an exit receipt.

At actual wrapper entry, privately record helper identity, generation, real self,
real SP, `*sp` runtime/method input, independently read actual fragment and current
quick head. Revalidate the exact frame family/input relation before reading other
metadata. Receiver/argument references must be copied immediately into calibrated
live source Handle slots before any suspension; the wrapper must re-read those
slots before calling original. Its owned observer roots do not masquerade as the
original helper's internal handles. Original handle provenance needs actual named
`EnsureInitialized(Handle<Class>)`, reflective-source callbacks or original
ShadowFrame/push observations; missing inner-object lifetime proof rejects.

Keep an episode active through the original C++ call. A genuine named
`Thread::PushManagedStackFragment` or `ManagedStack::PushManagedStackFragment`
hook, after original returns, records its actual fragment argument and new link;
match it with the enclosing helper and real `ArtMethod::Invoke` ingress. The
retained muyu image has those Push names but no corresponding Pop name in the
checked inventory. Use actual helper/Invoke return and verified restored source
head for normal exit, not a guessed Pop address. Nonreturning/long-jump paths need
an observed original lifetime boundary; C++ RAII is insufficient. Stopped matching
requires exact active generation/self/SP/fragment and every original walker
callback, including shadow/inline/transition entries. Old already-entered helpers
without an observed ingress remain unproved.

An interface helper's successful exit proves **selection**, not managed entry.
Fixed16 ARM64 assembly restores its callee-save frame then tail-branches to the
returned entry. That entry can be `art_quick_to_interpreter_bridge`, which creates
a new runtime refs-and-arguments frame while retaining the actual selected method
and pushes a ShadowFrame fragment before interpretation. Original walks can visit
the managed shadow fragment and a transition, then encounter that older bridge;
the immediate physical adjacency helper may therefore report `hasYounger=false`
after clearing at those boundaries. Bridge entry, shadow lifetime and actual
callee/source/root identity must be observed separately. Neither a public IfTable
match nor equal declaring Class proves that tail-branch entry happened.

The two setup macros in
[fixed16 ARM64 quick entrypoints:45–70](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/arch/arm64/quick_entrypoints_arm64.S)
distinguish placeholders from real methods: interpreter bridge uses the runtime
placeholder at 1891–1908; direct proxy and Generic JNI stubs use
`WITH_METHOD_IN_X0` at 1642–1643 and 1766–1767. Thus the observed placeholder does
not identify that physical frame as a direct proxy/native-method frame. It still
cannot rule out resolver/bridge nesting or managed callbacks merely by geometry.

## Already active helpers at agent installation

The current long-lived boundary may have entered before agent installation.
Installing the proposed entry hooks cannot recover that original ingress and is
**not a fix for that existing episode**. Restarting an app does not establish
coverage either: its inherited/startup managed frames can precede agent loading.
A future ledger must never fabricate an earlier generation for those frames.

Original WalkStack callbacks, actual shadow-frame method identities and the
complete calibrated ManagedStack chain can establish that interpreter-managed
frames exist in a younger fragment and that an older refs-and-arguments quick
frame is linked beneath them. They do not expose the native code that created
that fragment. In fixed16 `artQuickToInterpreterBridge:782–812`, the original
`method`, `non_proxy_method`, CodeItem accessor, stack-local `ManagedStack fragment`
and actual ShadowFrame are distinct continuation inputs; the original push at
810 and shadow push at 811 link the frame, but neither ManagedStack's stored
link/shadow fields nor its original walker callbacks retain the helper identity.
The bridge also has a separate deoptimization branch at 784–786, with an original
push in `HandleDeoptimization:723`. Its post-interpreter continuation at 815–841
checks deoptimization and retains caller/method/result/exception dependencies.

Other fixed16 helpers also push actual fragments: polymorphic at 2466, hidden
receiver at 2608 and custom at 2670. The authenticated caller DEX opcode excludes
incompatible direct dispatch candidates, but it does not by itself recover an
intervening native resolver/initializer/bridge continuation or its original local
handle ownership. These are topology ambiguity examples, not a claim that all
those helpers are valid direct targets of the observed invoke-interface opcode.
A shadow method equaling a legal interface implementation therefore still cannot
create the missing native owner receipt.

The exact missing observation is **an original creator/continuation owner bound
to this actual runtime SP, original fragment and live ShadowFrame**, together
with its real method/handle inputs and lifetime. New diagnostic callbacks should
record actual quick versus shadow entries, original method identity, actual
shadow pointer if a calibrated original getter exists, actual transition order,
and the separately read complete fragment chain. This can falsify the bridge
hypothesis and report the precise missing owner; it cannot turn a compatible
shape into a positive helper grant. The original quick PC resets to zero at each
fragment head (`stack.cc:821–826`), and saved LR points to the managed caller
continuation, so neither value supplies the current C++ helper instruction.

There is currently no demonstrated named read-only ART accessor that returns
that already active bridge's original native continuation owner and inputs.
Hooking an interpreter/fragment entry later only proves the event actually
observed then; it cannot certify an outer bridge that was already active unless
the original callback's declared contract independently exposes and binds that
outer owner. A retroactive capability needs such an additional source-backed
observation and calibration. Until obtained, the current pre-agent episode remains
strictly unproved even if all shadow/transition diagnostics match the bridge
hypothesis. New-entry ledger coverage must be reported separately from current
device compatibility.

## Reuse across Android 11–17

Seven fixed-commit `quick_trampoline_entrypoints.cc` files were individually
hash-checked and their original declarations compared; the evidence is under
`build/evidence/compatibility/20261011/runtime-helper-cross-version/`.
The following four typed declarations are common to all seven ARM64 routes:
interface trampoline, interface-with-access-check trampoline, quick resolution
trampoline and quick-to-interpreter bridge. Their actual helper bodies and native
continuation contracts are not interchangeable. The common episode owner can
reuse hook/lifetime/generation/root matching and typed ingress declarations while
capability owners retain the observed source family's specific dependencies.

| Android | Interface / access-check / resolution / interpreter declaration lines | Fixed source |
| --- | --- | --- |
| 11 | 2404 / 2353 / 1220 / 648 | [official commit](https://android.googlesource.com/platform/art/+/34f62af4d6425087667b7eb3998fcf58d61a2fa6/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 12 | 2290 / 2254 / 1219 / 647 | [official commit](https://android.googlesource.com/platform/art/+/8d44c3d61d4bde412f06190cd4acc4afc2ea469c/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 13 | 2353 / 2317 / 1217 / 650 | [official commit](https://android.googlesource.com/platform/art/+/88baac34da072b41917b4adc1987c55546266b33/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 14 | 2200 / 2164 / 1118 / 673 | [official commit](https://android.googlesource.com/platform/art/+/3c05e56adf5b268ec5b20bf8aec460815e45161c/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 15 | 2212 / 2178 / 1135 / 693 | [official commit](https://android.googlesource.com/platform/art/+/bebbc3cc49f2d9d5420197df0a336fbc3fcbea40/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 16 | 2288 / 2254 / 1203 / 761 | [official commit](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| 17 | 2343 / 2309 / 1205 / 760 | [official commit](https://android.googlesource.com/platform/art/+/b753cf97923c3695338d21466fa14c57b480a59a/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |

Place this lifecycle service under `platform/common`; keep only actual declaration
or layout differences in existing ABI/source capability owners. Do not create
seven copies, introduce OEM/BuildID grants or infer symbol offsets. API32/12L and
non-ARM64 ABIs remain excluded. The declaration comparison proves reuse potential,
not installed hook coverage or supported native helper continuation on any route.

## Source and verification limits

The exact Android 16 assembly and visitor anchors above were read from their
local cached files, and both SHA-256 values match the existing source manifests
for commit `ed6c006bd06ae060bd9698fd2cb25c4865512ec3`:

| Source | SHA-256 | Read anchors |
| --- | --- | --- |
| `runtime/arch/arm64/quick_entrypoints_arm64.S` | `a874689c1264564a8e75704a83fefd34a35d9ff63b9c1587c23a58883b65d60f` | 381–398, 1190–1239 |
| `runtime/stack.cc` | `58822c68f2a2dc8a5affe9bb3c25f9b310089b6d30f5f3ee79e3755122933f43` | 759–798, 821–826, 985–995 |

The assembly hash was compared with `deoptimize-if-needed/additional-sources.json`;
the visitor hash was compared with `modern-frame-source/manifest.json`, both
under `build/evidence/compatibility/20261008/`. This records fixed-source evidence,
not an actual device helper-episode observation.

The fixed Android 16 C++ sources are now available: **24 official files** were
retrieved at the same commit, and all decoded file SHA-256 values were recomputed
and matched `build/evidence/compatibility/20261011/fixed16-runtime-helper-source/manifest.json`.
Direct HTTPS initially timed out because the terminal did not use the existing
system proxy; explicitly using that existing proxy completed the official Gitiles
fetches without changing system configuration. Three guessed paths returned actual
HTTP 404; the correct dex-cache resolver and quick callee-save header paths are
included below. No Android 13/17 body substitutes for an Android 16 contract.

| Verified fixed source |
| --- |
| [runtime/entrypoints/quick/quick_field_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_field_entrypoints.cc) |
| [runtime/entrypoints/quick/quick_trampoline_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_trampoline_entrypoints.cc) |
| [runtime/entrypoints/quick/quick_alloc_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_alloc_entrypoints.cc) |
| [runtime/entrypoints/quick/quick_alloc_entrypoints.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_alloc_entrypoints.h) |
| [runtime/entrypoints/entrypoint_utils.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/entrypoint_utils.cc) |
| [runtime/entrypoints/entrypoint_utils-inl.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/entrypoint_utils-inl.h) |
| [runtime/entrypoints/entrypoint_utils.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/entrypoint_utils.h) |
| [runtime/quick_exception_handler.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/quick_exception_handler.cc) |
| [runtime/quick_exception_handler.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/quick_exception_handler.h) |
| [runtime/runtime-inl.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/runtime-inl.h) |
| [runtime/class_linker.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/class_linker.cc) |
| [runtime/reflective_handle_scope-inl.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/reflective_handle_scope-inl.h) |
| [runtime/reflective_handle_scope.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/reflective_handle_scope.h) |
| [runtime/arch/quick_field_entrypoints.S](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/arch/quick_field_entrypoints.S) |
| [runtime/entrypoints/quick/quick_dexcache_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_dexcache_entrypoints.cc) |
| [runtime/entrypoints/quick/quick_thread_entrypoints.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/quick_thread_entrypoints.cc) |
| [runtime/entrypoints/quick/callee_save_frame.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/entrypoints/quick/callee_save_frame.h) |
| [runtime/thread-inl.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/thread-inl.h) |
| [runtime/stack.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/stack.h) |
| [runtime/handle.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/handle.h) |
| [runtime/handle_scope.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/handle_scope.h) |
| [runtime/art_method.cc](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/art_method.cc) |
| [runtime/thread.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/thread.h) |
| [runtime/managed_stack.h](https://android.googlesource.com/platform/art/+/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/managed_stack.h) |

The complete source inventory enables future typed episodes; it does not make
these helpers supported. The first contracts must respect the actual source:

- `quick_dexcache_entrypoints.cc:210–226` declares
  `artInitializeStaticStorageFromCode(mirror::Class* klass, Thread* self)`.
  Its first argument is a **Class pointer**, not an index inferred from the
  assembly macro comment. It retains an actual Class Handle across
  `EnsureInitialized(true,true)` and returns null on failure or the re-read root
  on success. The class/initializer state and native-root dependencies still need
  a concrete same-Plan closure.
- `entrypoint_utils-inl.h:432–461` puts the resolved field into a
  `StackArtFieldHandleScope` / `ReflectiveHandle` before class initialization and
  returns `resolved_field_handle.Get()` afterwards. This is not evidence of a
  universally unprotected cached ArtField pointer. The actual reflective target
  update (`reflective_handle_scope-inl.h:35–49`) and member semantics, including
  removal/type/staticness/offset, must be bound to the same Plan.
- `class_linker.cc:6084–6128` resumes after the original clinit Invoke with an
  ObjectLock, exception wrapping and Class status/visibility updates.
  `WrapExceptionInInitializer` at 584–607 may replace a non-Error Throwable with
  `ExceptionInInitializerError`. `EnsureInitialized` at 6424–6470 explicitly
  clears a failure exception when its actual flags are not both true. Episodes
  must bind those flags and the complete initializer continuation; the ordinary
  JNI same-Throwable receipt cannot establish this native contract.
- `quick_trampoline_entrypoints.cc:1203–1205,1355–1389` retains a selected
  `ArtMethod* called` through initialization, then reads its class or selects its
  executable code and stores it back into the callee-save frame. Actual selected
  method, caller/DEX, arguments and executable-entry dependencies remain unproved
  until a typed owner binds them to the Plan. The original clinit Invoke alone
  does not reveal this selected method.

`Thread::PushManagedStackFragment` (`thread.h:1205–1209`) forwards the actual
fragment argument. `ManagedStack::PushManagedStackFragment`
(`managed_stack.h:64–76`) copies the original head into that supplied fragment,
clears the live head and links it to the actual fragment.
`ArtMethod::Invoke` pushes at `art_method.cc:380`, calls the original invoke stub
at 422/424 and pops at 445. Both named Push leaves occur in the retained muyu
Android 16 ELF name inventory, but either callsite may be inlined; actual hook
coverage and after-original event provenance must still be independently
calibrated. A matching expected pointer or an already-entered helper is not an
observed episode.

The previously missing exact16 `QuickExceptionHandler` is also available:
`quick_exception_handler.cc:84–101` records the original null-method upcall SP/PC,
256–259 restores the exception when it is not consumed, and 810–818 prepares the
original SP/PC/arg0 long jump. This establishes original ART mechanics; it does
not prove an arbitrary native resolver/ClassLinker continuation safe, and unwind
callbacks may change the exception before restoration.

No device measurement in this audit established an escaping older runtime helper.
Production therefore still rejects `escape && older Runtime`; none of these
source observations grants permission to bypass that boundary.

Required tests: runtime younger than the escaping frame remains unaffected;
unknown older runtime episode rejects; matching geometry with a different helper,
thread, fragment, callee or epoch rejects; recursive episodes do not alias;
retired cached field/entry dependency rejects; a concretely unchanged or observed
post-return revalidated dependency passes; missing actual exit rejects. Actual
Android 16 tests must capture the helper ingress before running the source
transaction, not install the ledger after the helper has already entered.
