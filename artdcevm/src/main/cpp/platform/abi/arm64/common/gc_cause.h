#pragma once
// Fixed android-13.0.0_r1 runtime/gc/gc_cause.h. The collector observation
// boundary forwards the original value without interpreting or selecting it.
namespace art::gc {
enum GcCause {
 kGcCauseNone,kGcCauseForAlloc,kGcCauseBackground,kGcCauseExplicit,
 kGcCauseForNativeAlloc,kGcCauseCollectorTransition,kGcCauseDisableMovingGc,
 kGcCauseTrim,kGcCauseInstrumentation,kGcCauseAddRemoveAppImageSpace,
 kGcCauseDebugger,kGcCauseHomogeneousSpaceCompact,kGcCauseClassLinker,
 kGcCauseJitCodeCache,kGcCauseAddRemoveSystemWeakHolder,kGcCauseHprof,
 kGcCauseGetObjectsAllocated,kGcCauseProfileSaver,kGcCauseRunEmptyCheckpoint
};
static_assert(sizeof(GcCause)==4);
}
