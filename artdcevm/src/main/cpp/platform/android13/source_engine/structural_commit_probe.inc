// Borrowed transaction roots only; no commit behavior in this calibration.
extern "C" bool ArtDcevmSourceProbeStructuralCommit(const void* raw,
    const artdcevm::android13::StructuralCommitRoots* roots) {
  static_assert(!art::kIsDebugBuild); // ScopedAssertNoThreadSuspension is a release no-op.
  static_assert(sizeof(art::ClassStatus)==sizeof(uint8_t));
  static_assert(art::kAccObsoleteObject==0x200000);
  static_assert(art::Thread::InterpreterCacheOffset<art::PointerSize::k64>().Int32Value()==2608);
  static_assert(RedefinitionDataHolder::kSlotMirrorClass==4);
  static_assert(RedefinitionDataHolder::kSlotNewInstanceObjects==10);
  auto& iter=*static_cast<const RedefinitionDataIter*>(raw);
  return iter.GetMirrorClass().Ptr()==roots->mirror &&
      iter.GetOldInstanceObjects().Ptr()==roots->old_instances &&
      iter.GetNewInstanceObjects().Ptr()==roots->new_instances &&
      iter.GetOldClasses().Ptr()==roots->old_classes && iter.GetNewClasses().Ptr()==roots->new_classes;
}
