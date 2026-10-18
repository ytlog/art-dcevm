# Compatibility conclusions

English | [中文](COMPATIBILITY.md)

This page summarizes concise public conclusions from existing verification records. It is not a run log and does not claim that the current commit was retested on every device. See [LIMITATIONS.en.md](LIMITATIONS.en.md) for semantics and rejection rules, [TESTING.en.md](TESTING.en.md) for procedures, and the [engine design](STRUCTURAL_REDEFINE_ENGINE.en.md) for implementation invariants.

## Evidence scope

Conclusions apply to tested arm64 devices and their ART configurations, not every OEM or ART Mainline module. A platform API is not an ART source version. Source declarations, offline symbols and successful compilation cannot replace device execution. Ordinary-class results do not establish interface-target compatibility.

After initialization, callers should still submit `RedefineClasses` directly: `0` means success; otherwise read the error code and `lastRejection`. Neither this table nor diagnostic-only `capabilities()` is a pre-submission admission check.

## Platform coverage

Every “passed” entry covers normal and CheckJNI modes. “Interfaces” includes the complete interface suite, declaration checks and failure recovery. The collector column records only established conclusions, without inferring extra coverage from defaults, GC counts or zero marking reads.

| Android / API | Ordinary classes and Thread | Interface targets | Collector evidence and boundaries |
| --- | --- | --- | --- |
| 11 / 30 | Complete ordinary suite and Thread rejection/recovery checks passed | Inactive; fixed declarations prepared | Ordinary results do not establish interface scope/holder or failure cleanup |
| 12 / 31 | Complete ordinary suite and Thread rejection/recovery checks passed | Inactive; fixed declarations prepared | Same boundary |
| 12L / 32 | Complete ordinary suite and Thread rejection/recovery checks passed | Inactive; uses the prepared Android 12 declarations | Same boundary |
| 13 / 33 | Passed | Complete suite and failure recovery passed; active | Tested configuration; no additional claim of both CC and CMC coverage here |
| 14 / 34 | Passed | Complete suite and failure recovery passed; active | Separate CC and CMC coverage; Compose `interface abstract batch` passed |
| 15 / 35 | Passed | Complete suite and failure recovery passed; active | Separate CC and CMC coverage; Compose `interface abstract batch` passed |
| 16 / 36 | Passed | Complete suite and failure recovery passed; active | Tested configuration; no additional claim of both CC and CMC coverage here |
| 17 / 37 | Existing first stage passed under both collectors and JNI modes; corrected pressure fixture needs a rerun | Inactive; fixed declarations prepared | CC and generational CMC ran; CC marking overlap was not established |

Existing API 34 full runs covered 146 scenarios each and the older HandleScope/holder. API 35 covered the same full scenarios, 16-byte scopes and three holder flags. Their abstract-method acceptance respectively retains the named link-error service requirement and uses linked dispatch arrays; this document does not change the runtime registry.

## Covered scenarios

- Ordinary classes: field/method addition, deletion and restoration; types/signatures; native/Java and abstract/concrete transitions; modifiers; superclass widening (including a batched subclass that only changes members, batched supertypes or new-chain classes that change only method bodies, and structural new-chain classes below the old superclass, including a new superclass rebuilt with its ancestor; 66 when a supertype changes structurally or a subclass moves too); old field values/reference identity; zero new fields; compatible old values kept when a reference field changes type (null otherwise); cached references and later GC. After Thread rejection, old definitions remain usable and later transactions succeed.
- Interfaces: top-level/nested bodies and supported member changes; methods switching among default, static and private (71 with a caller outside the batch, accepted with it); parent/child interfaces, multiple groups, batch implementations/subclasses/unrelated classes; copied defaults, mirandas, subinterface precedence, real bridges, lambdas and large implementation groups. `InterfaceEvolutionFixture`, `interfacelambda`, `interface-stress` and `interface-stress-add` provide this coverage, including stress samples with 200 implementations. Merged rebuilds cover intersecting interface groups whose instances one interface covers (with an owner that is not the first batch interface), multi-round instance collection without a covering owner (unrelated interfaces sharing an implementation; an interface and a structurally edited outside superclass with separate instances), and the 66 rejection for a superclass change. Deleting, re-signing and restoring interface defaults, abstract/default switches (including the 63 rejection for a missing implementation), interface static field additions/deletions, and added parent interfaces (63 for a missing implementation, 66 for removal; a class or interface adding an interface the batch rebuilds links its replacement) are covered on API 33/36 for calls, cached reflection and GC; API 34/35 have not been re-run.
- Abstract methods: a more specific abstract declaration masking a distant default, removal of the last implementation, missing bridges, loader identity, rejection in a later group and atomic rollback. Cached reflection, MethodHandle, JNI IDs and warmed bytecode are checked before/after failure, followed by corrected or withdrawn retries.
- Recovery: real DEX verification errors (62), failures after linking/allocation, during instance allocation or mapping checks, Native scope-construction failure, Thread/HandleScope/holder rejection, and late implementation/subclass retries and exhaustion. Subsequent superclass/interface transactions, old instances and GC are checked. An API 33 control APK with cleanup disabled crashes in the failure test, supporting the requirement to discard private replacements.
- JNI IDs: API 33 reproduces the cached-call risk in pointer mode and verifies rejection 79 for method and field-only interfaces, with successful index-mode controls. Old JNI calls, GC and ordinary transactions remain usable after rejection. This guard is not API 37 interface acceptance evidence.

## Compiler sample

Existing compilation with Kotlin 2.3.21 (`-jvm-default=enable`, JVM target 1.8) and D8 8.10.9 showed that appending a lambda in an interface default generates a private static implementation, a public static synthetic `$r8$lambda$` bridge and a synthetic class. Insertion can also change signatures or remove old bridges. New interface bodies can generate `DefaultImpls` and implementation bridges. This conclusion is limited to that compiler sample; see the [interface lambda design](STRUCTURAL_REDEFINE_ENGINE.en.md#lambdas-in-interface-defaults) for new-class delivery, atomic submission and old-closure semantics.

## Unproven scope

- Android 11 and 12/12L still need device validation of complete interface transactions, actual root traversal, scope/holder and cleanup. Native JNI array preallocation and exact signature adaptation only remove the corresponding preparation obstacles.
- Merged rebuilds (intersecting interface groups and joined structural superclasses) are device-verified only on API 33/36; API 34/35 use the same common implementation but have not been re-run.
- Android 17 still needs the complete interface suite, abort recovery and Compose under the correct JNI ID mode. Interfaces remain inactive.
- The old API 37 pressure fixture could coalesce `System.gc()` requests, and its total GC count included ART transaction GC. It did not establish independent worker pressure or CC marking overlap. The direct-request fixture that checks pre-transaction increments still needs device execution on that release. Earlier zero-overlap observations do not establish a Thread-offset error.
- GC completion logs do not establish marking intervals or object forwarding. Zero CC marking reads under CMC establish neither failure nor CMC coverage. A pressure run does not prove overlap in every transaction.
- Injected allocation failures do not establish recovery from real Java/native memory exhaustion. Other ART module updates, untested OEMs and releases require independent validation. One passing Compose scenario does not establish complete-app or all hot-reload behavior.

Raw logs, APK hashes, per-device reports and individual verification records stay in local ignored directories. Public conclusions contain no device identifiers, connection details or local machine paths. Follow the [new-release validation order](TESTING.en.md#new-release-validation-order) before updating these conclusions.
