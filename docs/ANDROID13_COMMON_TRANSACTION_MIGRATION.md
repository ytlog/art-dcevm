# Android13 common transaction migration

The old full route and the common owner are process-wide alternatives. They must not be installed together. Android13's build/profile identity is not proof of a complete plan's type-storage or receiver recovery dependencies.

## Current owner selection

`CapabilityBackend::Prepare` creates the public-oracle-backed common engine without installing its evolution proxies. `InitializeCapabilities` currently tests `Engine::Source13Candidate` and calls `PrepareSource` for that family. `PrepareSource` constructs `Android13Backend`, attaches it, and initializes the old private runtime. `source_ready_` makes every evolution operation call `source_->Apply`, bypassing `NativeEngine::Apply`, RunState, and the common typed-storage proof. Explicit `InitializeEvolution` takes the same legacy path. `EnableValidationHooks` can also construct the legacy owner before full initialization.

The existing `common-owned` device scenario still calls `initializeCapabilities` before asserting the public `runtime-capabilities` route. On Android13 that initializer automatically selected legacy, so the route assertion stopped the test before any common transaction was exercised. An observed legacy assertion failure is not evidence of a failed common source check.

## Overlapping private ownership

| Boundary | Legacy owner | Common owner |
| --- | --- | --- |
| Original field/method check | Android13 `ValidationHooks` | `native_owned_removal` |
| Original collection/final update | legacy evolution/structural commit | `native_owned_removal` plus RunState |
| Original Run/Finish/Allocate/Holder | hierarchy batch/allocation hooks | hierarchy session/private ledger |
| Original cache publication | legacy original continuation | private ledger + typed source GC publication |
| Source thread stop / final cleanup | Android13 fixed runtime hooks | calibrated source stop + required cleanup |
| Retired member dispatch | Android13 retired member hooks | common retirement/entry/alias owners |

Switching a single feature inside Apply is not a migration: already-installed legacy proxies and allocated transaction owners would remain. The first shared step provided here is the explicit debug-only `NativeBridge.initializeCommonCapabilities(calibration)` whole-owner initializer. Default capabilities/full initialization remains unchanged. The debug initializer selects common before any legacy installation. A previously selected legacy diagnostic, failed or initialized owner refuses that switch and requires restart. A later explicit full initializer must retain its existing refusal when a common subset is active; common `Activate` alone does not prove full evolution.

## Common dependency closure before claiming Android13 narrowing

The common path must complete original ordinary-source priming and loading observation, real stop/GC publication profile, exact source roots and allocator calibration, independent cache/writer witnesses, full frame readers, public member identity staging, same-plan source declaration/cache mapping, final stopped graph authority, typed whole-heap field/static/array storage proof, receiver bytecode/recovery closure, and exact final commit/refund cleanup. Each is owned by the existing common service. Missing evidence must retain its specific original refusal. Enabling the route does not enable these proofs.

The existing `common-owned` scenarios now attach public JVMTI then use that debug-only whole-owner initializer; their public route and behavioral assertions are unchanged. Run every scenario in a fresh process with `--scope common-owned`. They require public route identity, actual successful operations and continued Java/JNI behavior. Hierarchy narrowing additionally requires legal Object storage to succeed, all three retained ancestor storage forms to refuse before mutation, and fresh legal narrowing after clearing the forbidden cells. The old full route's safety withdrawal remains until this complete transaction passes. Do not relabel common activation as full initialization or weaken these assertions to claim migration completion.

## Validation scope

The actual CapabilityBackend TU host suite exercises both initialization orders, a source-family candidate selecting the whole common owner explicitly, an actual common Apply refusal with zero legacy fallback, default legacy initialization retained, failed legacy initialization refusing later common selection, and cross-thread/reentrant exclusion for the new entry. Public/native engine leaves are test doubles; this establishes routing and one-owner selection, not Android13 frame/cache/storage readiness. ARM64 debug agent/backend compilation confirms the public JNI entry is coherent. The actual seven Android13 scenarios have not yet run through this new route; no full-support claim is made.
