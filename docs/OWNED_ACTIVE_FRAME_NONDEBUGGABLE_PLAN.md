# Non-debuggable owned compiled-frame qualification

The existing frame proof remains a JIT-owned carrier with two actual inlinees.
The debug APK is not a sufficient execution environment for that shape. Fixed
[AOSP14 HInliner::Run](https://android.googlesource.com/platform/art/+/android-14.0.0_r1/compiler/optimizing/inliner.cc)
returns immediately for a debuggable graph. Fixed
[JitCompiler::ParseCompilerOptions](https://android.googlesource.com/platform/art/+/android-14.0.0_r1/compiler/jit/jit_compiler.cc)
sets compiler debuggability from Runtime::IsJavaDebuggable when options do not
already request debuggable code. More warming does not enable this source policy.
The new diagnostic calls the fixed read-only native VMRuntime_isJavaDebuggable
leaf when available; it never changes runtime/compiler debug flags.

The next executable route should be an isolated non-debuggable test target with
the owned Java fixture and ARTDCEVM_TESTING bridge included deliberately. Its
instrumentation APK must target that package with the same signing identity.
Public JVMTI attach is unavailable in this environment; the current
RuntimeAccessInstrumentation automatically attaches it for --active-frames, so
that caller cannot simply be pointed at a release APK. A separate caller must
first perform the native production bootstrap and independently acquire the
native JIT/cache/runtime receivers in **that same process**. The concrete bridge
prerequisite is `OwnedEvolutionProofs().observed.receivers.jit_code_cache`, plus
successful runnable/exclusive strong-root calibration. A pointer/report from the
debug process must never be reused. This bootstrap/caller interface is not
implemented or qualified by the current diagnostic batch.

After those prerequisites are implemented, the independent caller may run the
unchanged actual DEX-indexed fixture/gate oracle. It must observe native runtime
Java-debuggable=false (or an independently proven non-debuggable compilation
policy), actual carrier JitCodeCache::ContainsMethod/ContainsPc ownership, two
distinct inline methods on the same physical frame, original/decoded/invoke DEX
PC agreement, all six original/physical int/reference reads, stack/register root
masks, and after-gate retained identity/value writes. Non-debuggable execution is
only a compiler precondition; it does not establish inlining or frame recovery.
A missing inlinee remains a refusal and may need real compiler observations to
tune the owned fixture. No physical-frame substitution or synthesized flag is
permitted.

An AOT route would need its own independently verified compiled method/header,
DEX provenance and PC ownership oracle. The current service deliberately requires
native JIT ownership, so an AOT header cannot pass by weakening that predicate.
AOT support must be an explicit additional source/ownership contract with real
inline/map/root evidence. Runtime debug flags or process-wide compiler options
must not be toggled merely to manufacture this proof.

Only after the entire executable caller/bootstrap batch is complete should one
frozen APK/test APK be built and run. This document plans that next interface; it
claims no non-debuggable device pass or complete evolution capability.
