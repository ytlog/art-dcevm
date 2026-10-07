#include "platform/android13/heap_reference_abi.h"
#include "mirror/object.h"
#include "mirror/object_reference.h"
#include "lock_word.h"
#include "read_barrier.h"
#include "heap_poisoning.h"

extern "C" int ArtDcevmSourceProbeHeapReferenceLayout() {
  using namespace artdcevm::android13::heap_reference_abi;
  static_assert(sizeof(art::mirror::Object) == sizeof(ObjectPrefix));
  static_assert(art::mirror::Object::MonitorOffset().Int32Value() == kMonitorOffset);
  static_assert(sizeof(art::mirror::HeapReference<art::mirror::Object>) == sizeof(uint32_t));
  static_assert(art::LockWord::kReadBarrierStateShift == kReadBarrierShift);
  static_assert(art::LockWord::kReadBarrierStateMask == kReadBarrierMask);
  static_assert(art::ReadBarrier::GrayState() == kGrayState);
  static_assert(art::ReadBarrier::NonGrayState() == 0);
  return art::kUseBakerReadBarrier && art::kUseReadBarrier &&
      !art::kUseTableLookupReadBarrier && !art::kIsDebugBuild &&
      !art::kPoisonHeapReferences ? 0 : 1;
}
