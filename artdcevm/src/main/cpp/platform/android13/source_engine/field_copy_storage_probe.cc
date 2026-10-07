#include "platform/android13/field_copy_storage_abi.h"
#include "platform/android13/field_copy_abi.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "art_field-inl.h"
#include "gc/accounting/card_table.h"
#include "gc/heap.h"
#include "mirror/object-inl.h"
#include "mirror/class-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "thread.h"
#include "thread-current-inl.h"
#include <cstring>
#include <initializer_list>

namespace storage_abi = artdcevm::android13::field_copy_storage_abi;
namespace metadata_abi = artdcevm::android13::field_copy_abi;
static_assert(art::Thread::CardTableOffset<art::PointerSize::k64>().Int32Value() ==
              storage_abi::kThreadCardTableOffset);
static_assert(art::mirror::Object::MonitorOffset().Int32Value() == storage_abi::kMonitorOffset);
static_assert(art::gc::accounting::CardTable::kCardShift == storage_abi::kCardShift);
static_assert(art::gc::accounting::CardTable::kCardDirty == storage_abi::kCardDirty);
static_assert(art::kAccVolatile == storage_abi::kVolatileFlag);
static_assert(!art::kPoisonHeapReferences);
static_assert(sizeof(art::mirror::HeapReference<art::mirror::Object>) == sizeof(uint32_t));
static_assert(sizeof(art::ArtField) == sizeof(metadata_abi::ArtField));
static_assert(art::ArtField::OffsetOffset().Int32Value() == offsetof(metadata_abi::ArtField, offset));

// Source declarations calibrate storage and barriers against the real runtime;
// they never provide a read/write callback for a production structural commit.
extern "C" int ArtDcevmSourceProbeFieldCopyStorage(
    JNIEnv* jni, jobject calibration, const artdcevm::android13::FieldCopyAccess* functions) {
  if (!functions || !functions->read || !functions->write ||
      !functions->transfer_monitor || !functions->barrier) return 1;
  jclass java_class = jni->GetObjectClass(calibration);
  if (!java_class) return 2;
  // Mutation checks use private temporary objects; the caller's calibration
  // instance and static fields retain their original values.
  jobject temporary_source = jni->AllocObject(java_class);
  jobject temporary_target = jni->AllocObject(java_class);
  jni->DeleteLocalRef(java_class);
  if (!temporary_source || !temporary_target || jni->ExceptionCheck()) {
    if (temporary_source) jni->DeleteLocalRef(temporary_source);
    if (temporary_target) jni->DeleteLocalRef(temporary_target);
    return 3;
  }
  int result = [&]() REQUIRES_SHARED(art::Locks::mutator_lock_) {
    art::ScopedObjectAccess soa(jni);
    auto object = soa.Decode<art::mirror::Object>(calibration);
    auto klass = object->GetClass();
    auto* value = klass->FindDeclaredInstanceField("value", "I");
    auto* wide = klass->FindDeclaredInstanceField("wide", "J");
    auto* counter = klass->FindDeclaredStaticField("counter", "I");
    if (!value || !wide || !counter) return 4;
    for (auto* field : {value, wide, counter}) {
      metadata_abi::ArtField fixed;
      std::memcpy(&fixed, field, sizeof(fixed));
      if (fixed.access_flags != field->GetAccessFlags() ||
          fixed.offset != field->GetOffset().Int32Value()) return 5;
    }
    if (functions->read(object.Ptr(), value, 'I') != 37 ||
        functions->read(object.Ptr(), wide, 'J') != 0x123456789ABCDEFULL ||
        functions->read(klass, counter, 'I') != 7) return 6;
    uintptr_t biased;
    std::memcpy(&biased, reinterpret_cast<unsigned char*>(soa.Self()) +
                            storage_abi::kThreadCardTableOffset, sizeof(biased));
    auto* table = art::Runtime::Current()->GetHeap()->GetCardTable();
    if (biased != reinterpret_cast<uintptr_t>(table->GetBiasedBegin())) return 7;
    auto source = soa.Decode<art::mirror::Object>(temporary_source);
    auto target = soa.Decode<art::mirror::Object>(temporary_target);
    functions->write(source.Ptr(), value, 'I', 0xfedcba98);
    functions->write(source.Ptr(), wide, 'J', 0xFEDCBA9876543210ULL);
    if (static_cast<uint32_t>(value->GetInt(source)) != 0xfedcba98 ||
        static_cast<uint64_t>(wide->GetLong(source)) != 0xFEDCBA9876543210ULL) return 8;
    functions->write(source.Ptr(), value, 'I', 0);
    functions->write(source.Ptr(), wide, 'J', 0);
    if (value->GetInt(source) != 0 || wide->GetLong(source) != 0) return 9;
    // Unlike a structural commit this calibration does not hold the exclusive
    // mutator lock. Moving managed monitor GC bits could race the collector,
    // even for private objects. Exercise the exact source-declared monitor
    // layout on aligned native headers instead; managed monitors stay untouched.
    const auto original_lock = art::LockWord::FromHashCode(
        0x12345, source->GetLockWord(false).GCState());
    uint32_t original_bits;
    static_assert(sizeof(original_lock) == sizeof(original_bits));
    std::memcpy(&original_bits, &original_lock, sizeof(original_bits));
    alignas(8) uint32_t native_source[2]{0, original_bits};
    alignas(8) uint32_t native_target[2]{0, 0};
    functions->transfer_monitor(native_target, native_source);
    if (native_target[1] != original_bits ||
        native_source[1] != storage_abi::kDefaultLockWord) return 10;
    const auto default_lock = art::LockWord::Default();
    uint32_t default_bits;
    static_assert(sizeof(default_lock) == sizeof(default_bits));
    std::memcpy(&default_bits, &default_lock, sizeof(default_bits));
    if (default_bits != storage_abi::kDefaultLockWord) return 11;
    functions->barrier(target.Ptr());
    if (!table->IsDirty(target.Ptr())) return 12;
    return 0;
  }();
  jni->DeleteLocalRef(temporary_source);
  jni->DeleteLocalRef(temporary_target);
  return result;
}
