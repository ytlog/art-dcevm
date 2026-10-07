// Included after RedefinitionDataIter's complete declaration. Read-only source
// calibration; it never supplies production metadata or makes policy decisions.
extern "C" bool ArtDcevmSourceProbeHierarchyTarget(void* raw, size_t index,
    const artdcevm::android13::hierarchy_allocation_abi::Target* snapshot) {
  namespace fixed=artdcevm::android13::hierarchy_allocation_abi;
  using Definition=std::remove_reference_t<decltype(std::declval<RedefinitionDataIter&>().GetRedefinition())>;
  static_assert(sizeof(std::vector<Definition>)==sizeof(fixed::Vector));
  static_assert(sizeof(Definition)==
      sizeof(artdcevm::android13::redefinition_abi::ClassRedefinition));
  static_assert(RedefinitionDataHolder::kSlotNewDexCache==fixed::kCacheSlot);
  static_assert(RedefinitionDataHolder::kSlotMirrorClass==fixed::kClassSlot);
  if(!raw || !snapshot) return false;
  auto& holder=*static_cast<RedefinitionDataHolder*>(raw);
  if(index>=static_cast<size_t>(holder.Length())) return false;
  RedefinitionDataIter iter(static_cast<int32_t>(index),holder);
  auto* owners=holder.GetRedefinitions();
  fixed::Vector vector;
  std::memcpy(&vector,owners,sizeof(vector));
  return vector.begin==reinterpret_cast<const unsigned char*>(owners->data()) &&
      vector.end==vector.begin+owners->size()*sizeof(Definition) &&
      vector.capacity==vector.begin+owners->capacity()*sizeof(Definition) &&
      snapshot->owner==&iter.GetRedefinition() && snapshot->klass==iter.GetMirrorClass().Ptr() &&
      snapshot->cache==iter.GetNewDexCache().Ptr() && snapshot->structural==iter.IsActuallyStructural();
}
