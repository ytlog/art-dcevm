// Included in ti_redefine.cc after RedefinitionDataIter is fully declared.

struct AllocationValidationProbe {
  static bool Check(void* raw, const artdcevm::android13::allocation_abi::State* state)
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    namespace fixed = artdcevm::android13::allocation_abi;
    using Holder = RedefinitionDataHolder;
    using Iterator = RedefinitionDataIter;
    static_assert(sizeof(Iterator) == sizeof(fixed::Iterator));
    static_assert(offsetof(Iterator, idx_) == offsetof(fixed::Iterator, index));
    static_assert(offsetof(Iterator, holder_) == offsetof(fixed::Iterator, holder));
    static_assert(sizeof(Holder) == sizeof(fixed::Holder));
    static_assert(offsetof(Holder, arr_) == offsetof(fixed::Holder, array_handle));
    static_assert(offsetof(Holder, redefinitions_) == offsetof(fixed::Holder, redefinitions));
    static_assert(offsetof(Holder, initialized_) == offsetof(fixed::Holder, initialized));
    static_assert(offsetof(Holder, actually_structural_) == offsetof(fixed::Holder, actually_structural));
    static_assert(offsetof(Holder, initial_structural_) == offsetof(fixed::Holder, initial_structural));
    static_assert(sizeof(std::vector<bool>) == sizeof(fixed::BitVector));
    static_assert(sizeof(art::StackReference<art::mirror::Object>) == sizeof(uint32_t));
    static_assert(art::mirror::Array::LengthOffset().Int32Value() == fixed::kArrayLengthOffset);
    static_assert(art::mirror::Array::DataOffset(sizeof(uint32_t)).Int32Value() == fixed::kReferenceArrayDataOffset);
    static_assert(art::mirror::Array::DataOffset(sizeof(uint64_t)).Int32Value() == fixed::kPointerArrayDataOffset);
    static_assert(art::mirror::Class::VTableOffset().Int32Value() ==
        offsetof(artdcevm::android13::validation_abi::ClassPrefix, vtable));
    static_assert(art::kAccAbstract == fixed::kAbstractFlag);
    static_assert(Holder::kNumSlots == fixed::kSlots);
    static_assert(Holder::kSlotOldInstanceObjects == fixed::kOldInstancesSlot);
    static_assert(Holder::kSlotOldClasses == fixed::kOldClassesSlot);
    static_assert(Holder::kSlotNewClasses == fixed::kNewClassesSlot);
    static_assert(art::ArtMethod::AccessFlagsOffset().Int32Value() ==
        offsetof(artdcevm::android13::art_method_abi::ArtMethod, access_flags));
    static_assert(art::ArtMethod::Size(art::PointerSize::k64) ==
        sizeof(artdcevm::android13::art_method_abi::ArtMethod));
    if (!raw || !state) return false;
    auto* iterator = static_cast<Iterator*>(raw);
    auto& holder = iterator->holder_;
    fixed::BitVector bits;
    std::memcpy(&bits, &holder.initial_structural_, sizeof(bits));
    if (bits.size != holder.initial_structural_.size() || !bits.begin) return false;
    for (size_t index = 0; index < bits.size; ++index) {
      const bool value = (bits.begin[index / 64] & (uint64_t{1} << (index % 64))) != 0;
      if (value != holder.initial_structural_[index]) return false;
    }
    if (state->initial_structural != iterator->IsInitialStructural() ||
        state->old_instances != iterator->GetOldInstanceObjects().Ptr() ||
        state->old_classes != iterator->GetOldClasses().Ptr() ||
        state->new_classes != iterator->GetNewClasses().Ptr()) return false;
    if (!state->initial_structural) return true;
    auto classes = iterator->GetNewClasses();
    for (auto klass : classes->Iterate()) {
      if (!klass->IsAbstract()) continue;
      if (klass->ShouldHaveEmbeddedVTable()) return false;
      auto table = klass->GetVTable();
      if (table == nullptr) continue;
      for (int32_t index = 0; index < klass->GetVTableLength(); ++index) {
        auto* method = klass->GetVTableEntry(index, art::kRuntimePointerSize);
        artdcevm::android13::art_method_abi::ArtMethod fixed_method;
        std::memcpy(&fixed_method, method, sizeof(fixed_method));
        if (fixed_method.access_flags != method->GetAccessFlags()) return false;
      }
    }
    return true;
  }

};
extern "C" bool ArtDcevmSourceProbeAllocationValidation(
    void* data, const artdcevm::android13::allocation_abi::State* state) {
  return AllocationValidationProbe::Check(data, state);
}
