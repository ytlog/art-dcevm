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
| 11 / 30 | Complete ordinary suite and Thread rejection/recovery checks passed | Enabled from fixed sources and stored-library names; not device-validated | Ordinary results do not establish interface scope/holder or failure cleanup |
| 12 / 31 | Complete ordinary suite and Thread rejection/recovery checks passed | Enabled from fixed sources and stored-library names; not device-validated | Same boundary |
| 12L / 32 | Complete ordinary suite and Thread rejection/recovery checks passed | Enabled with the Android 12 declarations; not device-validated | Same boundary |
| 13 / 33 | Passed | Complete suite and failure recovery passed; active | Tested configuration; no additional claim of both CC and CMC coverage here |
| 14 / 34 | Passed | Complete suite and failure recovery passed; active | Separate CC and CMC coverage; Compose `interface abstract batch` passed |
| 15 / 35 | Passed | Complete suite and failure recovery passed; active | Separate CC and CMC coverage; Compose `interface abstract batch` passed |
| 16 / 36 | Passed | Complete suite and failure recovery passed; active | Tested configuration; no additional claim of both CC and CMC coverage here |
| 17 / 37 | Existing first stage passed under both collectors and JNI modes; corrected pressure fixture needs a rerun | Enabled from fixed sources and stored-library names; not device-validated | CC and generational CMC ran; CC marking overlap was not established |

Existing API 34 full runs covered 146 scenarios each and the older HandleScope/holder. API 35 covered the same full scenarios, 16-byte scopes and three holder flags. Those historical results used the earlier 63 rejection for missing implementations; the current JBR call-time error semantics have not been re-run on API 34/35.

## Covered scenarios

- Ordinary classes: field/method addition, deletion and restoration; types/signatures; native/Java and abstract/concrete transitions; modifiers; superclass widening (including structural ancestors, new-chain classes and interface combinations; 66 remains for two moving targets in one old superclass subtree); old field values/reference identity; zero new fields; compatible old values kept when an instance reference field changes type (null otherwise), with separate JBR FieldCopier rules for static references; cached references and later GC. After Thread rejection, old definitions remain usable and later transactions succeed.
- Application `Thread` subclasses: structural changes of running and unstarted instances passed on API 36 in normal and CheckJNI modes.
- Interface removal: API 36 normal and CheckJNI runs cover interpreted and hot calls, boot code, later-loaded concrete subclasses, restoring interfaces and subsequent GC. Disabling dispatch sealing caused incorrect method dispatch in the control build. The current implementation adds persistent pre-publication protection for later concrete descendants of abstract ancestors and subsequent rebuilds; see the engine design for semantics and cost boundaries.
- Proxies: API 33/36 normal and CheckJNI runs cover handler method identities, missing abstracts throwing `AbstractMethodError`, added defaults, deletion, cached proxy classes and GC. Parent addition/removal/restoration, shared-parent reachability, multiple targets adding parents and batched parent redefinition pass. Cyclic hierarchies reject atomically with 61; old proxies and later transactions remain usable. Earlier Proxy-specific artifact evidence covers 22 Proxy scenarios, 275 full scenarios and 743 recovery checks per run. The new combination validation is recorded below; no new device claim is made for other releases.
- Interfaces: top-level/nested bodies and supported member changes; methods switching among default, static and private (instance-to-static old interface calls throw ICCE; old virtual calls naming the implementation lose the default and throw NSME; static-to-instance old static calls throw ICCE; default-to-private changes make old interface calls without access throw IAE and old virtual calls naming the implementation throw NSME; old active target frames remain unsupported and reject with 71 when detected); parent/child interfaces, multiple groups, batch implementations/subclasses/unrelated classes; copied defaults, mirandas, subinterface precedence, real bridges, lambdas and large implementation groups. `InterfaceEvolutionFixture`, `interfacelambda`, `interface-stress` and `interface-stress-add` provide this coverage, including stress samples with 200 implementations. Merged rebuilds cover intersecting interface groups whose instances one interface covers (with an owner that is not the first batch interface), multi-round instance collection without a covering owner (unrelated interfaces sharing an implementation; an interface and a structurally edited outside superclass with separate instances), and combinations of interface and implementation-ancestor superclass changes. Deleting, re-signing and restoring interface defaults, abstract/default switches, interface static field additions/deletions, and added/removed parent interfaces (classes, interfaces and Proxies follow the final batch interface graph, including newly added interfaces rebuilt by the batch) are covered on API 33/36 for calls, cached reflection and GC; API 34/35 have not been re-run.
- Historical abstract-method coverage includes more specific abstract declarations masking distant defaults, removal of the last implementation, missing bridges, loader identity and atomic rollback. The current implementation accepts missing implementations and default conflicts, with calls throwing `AbstractMethodError` and `IncompatibleClassChangeError` respectively; local per-run records identify final-artifact validation for each release.
- Recovery: real DEX verification errors (62), failures after linking/allocation, during instance allocation or mapping checks, Native scope-construction failure, Thread/HandleScope/holder rejection, and late implementation/subclass retries and exhaustion. Subsequent superclass/interface transactions, old instances and GC are checked. An API 33 control APK with cleanup disabled crashes in the failure test, supporting the requirement to discard private replacements.
- JNI IDs: API 33 reproduces the cached-call risk in pointer mode and verifies rejection 79 for method and field-only interfaces, with successful index-mode controls. Old JNI calls, GC and ordinary transactions remain usable after rejection. This guard is not API 37 interface acceptance evidence.

## Same-batch class combinations and JBR

Fixed baseline: JBR 21.0.11+10-b1163.116. The original v0/v1/harness fixtures were rebuilt and rerun locally on 2026-10-19. Android fixtures add migration sentinels and planned-abort coverage.

| Case | Batch | Fixed JBR | art-dcevm |
| --- | --- | --- | --- |
| s1 | C moves A → B (B extends A); A adds a field and method | Accept | 0 |
| s2 | C moves; its old subclass D also moves | Reject | 66 |
| s3 | C moves under B; B's interface I changes members | Accept | 0 |
| s4 | C moves under B; B moves under B2 | Accept | 0 |
| s5 | I changes members; its implementation C moves | Accept | 0 |
| s6 | I changes members; Base of unsubmitted Impl moves | Accept | 0 |

Intentional field difference: fixed JBR reads **3** from the inserted-parent field of the old C in s1/s4. This project requires **0** and preserves compatible fields of C and its old ancestor chain. This is acceptance parity with an explicit field-value difference, not identical migration semantics. The same frozen APK passes normal and CheckJNI full suites on M2007J1SC/API 33 (423 scenarios each) and 24091RPADC/API 36 (425 each), plus independent failure recovery and runtimeProbe 13/14/15 in both modes. The combination scenarios cover type identity, new-parent dispatch, added members, old/fresh instances, interface defaults where applicable, GC, and one mapping-plan abort per accepted combination. API 30–32, 34, 35 and 37 have no device verification of this change.

## Compiler sample

Existing compilation with Kotlin 2.3.21 (`-jvm-default=enable`, JVM target 1.8) and D8 8.10.9 showed that appending a lambda in an interface default generates a private static implementation, a public static synthetic `$r8$lambda$` bridge and a synthetic class. Insertion can also change signatures or remove old bridges. New interface bodies can generate `DefaultImpls` and implementation bridges. This conclusion is limited to that compiler sample; see the [interface lambda design](STRUCTURAL_REDEFINE_ENGINE.en.md#lambdas-in-interface-defaults) for new-class delivery, atomic submission and old-closure semantics.

## Unproven scope

- Android 11 and 12/12L still need device validation of complete interface transactions, actual root traversal, scope/holder and cleanup. Native JNI array preallocation and exact signature adaptation only remove the corresponding preparation obstacles.
- Merged rebuilds (intersecting interface groups and joined structural superclasses) are device-verified only on API 33/36; API 34/35 use the same common implementation but have not been re-run.
- Android 17 still needs the complete interface suite, abort recovery and Compose under the correct JNI ID mode. Its interface profile is enabled from fixed sources and stored-library names only.
- Private self-frame selection on API 34–35 runtimes that export both `FindMethodToCall` selectors follows the fixed sources but has not run on a device; only API 36 is device-verified.
- The old API 37 pressure fixture could coalesce `System.gc()` requests, and its total GC count included ART transaction GC. It did not establish independent worker pressure or CC marking overlap. The direct-request fixture that checks pre-transaction increments still needs device execution on that release. Earlier zero-overlap observations do not establish a Thread-offset error.
- GC completion logs do not establish marking intervals or object forwarding. Zero CC marking reads under CMC establish neither failure nor CMC coverage. A pressure run does not prove overlap in every transaction.
- Injected allocation failures do not establish recovery from real Java/native memory exhaustion. Other ART module updates, untested OEMs and releases require independent validation. One passing Compose scenario does not establish complete-app or all hot-reload behavior.

Raw logs, APK hashes, per-device reports and individual verification records stay in local ignored directories. Public conclusions contain no device identifiers, connection details or local machine paths. Follow the [new-release validation order](TESTING.en.md#new-release-validation-order) before updating these conclusions.

Interface/non-public class-method selection has its own `interfaceMethodSelection` capability, requiring named libart/libdexfile services and runtime declaration probes. New selection, late children, abstract/default transitions, parent defaults and rollback currently have normal and CheckJNI arm64 device coverage on API 33 (Android 13) and API 36 (Android 16). This new path has no device run here on API 30–32, 34–35 or 37; previous interface suite results do not establish that coverage.
