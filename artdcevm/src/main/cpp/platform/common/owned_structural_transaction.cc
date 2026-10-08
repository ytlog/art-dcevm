#include "platform/common/owned_structural_transaction.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/class_metadata_abi.h"
#include <cstring>
#include "platform/abi/arm64/android11/jni_object_decode.h"
namespace artdcevm::owned_structural_transaction {
namespace {
bool Mapped(const void* pointer,size_t needed) {
  size_t extent=0;
  return pointer&&method_identity::ReadableMappingExtent(pointer,&extent)&&extent>=needed;
}
template<class T> T Read(const void* object,size_t offset) {
  T result;std::memcpy(&result,static_cast<const char*>(object)+offset,sizeof(result));return result;
}
}
bool ReadStructuralTransaction(HookEngine& hooks,
    const modern_field_metadata::FieldMetadata& fields,const void* raw,
    StructuralTransaction* output,std::string* error) {
  const auto fail=[&](const char* message){if(error)*error=message;return false;};
  if(!output||!fields.Calibrated()||!Mapped(raw,sizeof(transaction_root::Iterator)))
    return fail("Missing calibrated structural transaction iterator");
  const auto& layout=fields.StorageModel()==modern_field_metadata::Model::kMerged?
      class_metadata::k17:class_metadata::kLegacy;
  static_assert(offsetof(class_metadata::Class11,component_type)==offsetof(class_metadata::Class17,component_type));
  size_t extent=0;method_identity::ReadableMappingExtent(raw,&extent);
  transaction_root::Iterator iterator;
  if(!transaction_root::ReadIterator(raw,extent,&iterator)||
      !Mapped(iterator.holder,sizeof(transaction_root::Holder)))
    return fail("Structural iterator has no live Holder mapping");
  method_identity::ReadableMappingExtent(iterator.holder,&extent);
  transaction_root::Holder holder;
  if(!transaction_root::ReadHolderHeader(iterator.holder,extent,&holder)||
      !Mapped(holder.array_handle,4)||!Mapped(holder.redefinitions,sizeof(transaction_root::Owners)))
    return fail("Structural Holder native roots are not readable");
  auto root=reinterpret_cast<void*(*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
  if(!root)return fail("Missing native transaction handle root barrier");
  void* array=root(const_cast<uint32_t*>(holder.array_handle));
  if(!Mapped(array,12))return fail("Structural root array prefix is not mapped");
  const int32_t slots=Read<int32_t>(array,8);
  if(slots<=0||slots%abi::arm64::android11::kRootSlots||
      !Mapped(array,12+size_t(slots)*4))return fail("Structural root array has an invalid 13-slot partition");
  // The complete owned field oracle selected the split/merged Class source prefix.
  // Every managed array in the native Holder must be a reference array.
  auto reference_array=[&](void* objects)->bool {
    if(!Mapped(objects,12))return false;
    void* klass=fields.Reference(objects,0);
    if(!Mapped(klass,layout.size))return false;
    void* component=fields.Reference(klass,offsetof(class_metadata::Class17,component_type));
    return Mapped(component,layout.size)&&
        !(Read<uint32_t>(component,layout.primitive_type)&0xffff);
  };
  if(!reference_array(array))return fail("Structural holder is not a reference array");
  const size_t count=size_t(slots)/abi::arm64::android11::kRootSlots;
  // The real platform vector span and independent rooted partition count
  // select a source declaration. Do not derive owner stride from the SDK.
  transaction_root::Owners owners;std::memcpy(&owners,holder.redefinitions,sizeof(owners));
  uintptr_t first=uintptr_t(owners.begin),last=uintptr_t(owners.end);
  if(!first||last<first||(last-first)%count)return fail("Native owner vector has no exact live stride");
  const size_t stride=(last-first)/count;
  // All fixed 11--17 declarations have this same 72-byte geometry;
  // differing final policy bits are not read by this managed-root adapter.
  static_assert(sizeof(abi::arm64::android11::ClassRedefinition)==sizeof(abi::arm64::android15::ClassRedefinition));
  static_assert(sizeof(abi::arm64::android13::ClassRedefinition)==sizeof(abi::arm64::android15::ClassRedefinition));
  if(stride!=sizeof(abi::arm64::android15::ClassRedefinition))
    return fail("Native owner vector does not match the fixed shared source geometry");
  constexpr auto family=transaction_root::OwnerFamily::kAndroid15;
  StructuralTransaction result;
  if(!transaction_root::ReadMetadata(holder,count,family,&result.metadata))
    return fail("Modern structural owner ranges or bitvectors are inconsistent");
  const auto& m=result.metadata;
  if(!Mapped(m.owners.begin,count*m.stride)||
      !Mapped(m.initialized.begin,8*((count+63)/64))||
      !Mapped(m.actually_structural.begin,8*((count+63)/64))||
      !Mapped(m.initial_structural.begin,8*((count+63)/64)))
    return fail("Modern structural metadata exceeds actual mappings");
  if(!transaction_root::ReadRoots(m,size_t(iterator.index),
      [&](size_t slot){return fields.Reference(array,12+4*slot);},&result.roots)||
      !result.roots.initialized||!result.roots.actually_structural||!result.roots.initial_structural)
    return fail("Callback is not an initialized initial structural transaction");
  auto elements=[&](void* objects,std::vector<void*>* values)->bool {
    if(!reference_array(objects))return false;
    const int32_t length=Read<int32_t>(objects,8);
    if(length<0||!Mapped(objects,12+size_t(length)*4))return false;
    for(int32_t i=0;i<length;++i){void* object=fields.Reference(objects,12+4*size_t(i));
      if(!Mapped(object,8))return false;values->push_back(object);}
    return true;
  };
  auto& plan=result.plan;
  plan.mirror=result.roots.values[1];plan.new_classes_root=result.roots.values[5];
  using Mirror=abi::arm64::android11::ReleaseObjectPointer (*)(void*);
  auto mirror=reinterpret_cast<Mirror>(hooks.FindSymbol("libopenjdkjvmti.so",
      "_ZN12openjdkjvmti9Redefiner17ClassRedefinition14GetMirrorClassEv"));
  if(!mirror||mirror(result.roots.owner).reference!=uintptr_t(plan.mirror))
    return fail("Native owner mirror disagrees with rooted structural target");

  if(!Mapped(plan.mirror,layout.size)||!elements(result.roots.values[4],&plan.old_classes)||
      !elements(result.roots.values[5],&plan.new_classes)||
      !elements(result.roots.values[2],&plan.old_instances)||
      !elements(result.roots.values[3],&plan.new_instances)||plan.old_classes.empty()||
      plan.old_classes.size()!=plan.new_classes.size()||plan.old_instances.size()!=plan.new_instances.size())
    return fail("Structural callback root dimensions are inconsistent");
  void* meta=fields.Reference(plan.mirror,0);
  for(size_t i=0;i<plan.old_classes.size();++i) {
    void* old=plan.old_classes[i];void* replacement=plan.new_classes[i];
    if(!Mapped(old,layout.size)||!Mapped(replacement,layout.size)||fields.Reference(old,0)!=meta||
        fields.Reference(replacement,0)!=meta)return fail("Structural class mapping contains a non-Class root");
    const uint32_t status=Read<uint32_t>(old,layout.status)>>28;
    if(status==2||status==3)return fail("Structural source Class is erroneous");
  }
  for(size_t i=0;i<plan.old_instances.size();++i) {
    void* old_type=fields.Reference(plan.old_instances[i],0);bool matched=false;
    for(size_t c=0;c<plan.old_classes.size();++c)if(old_type==plan.old_classes[c]) {
      if(fields.Reference(plan.new_instances[i],0)!=plan.new_classes[c])return fail("Structural replacement instance has the wrong Class");
      const uint32_t before=Read<uint32_t>(old_type,layout.access_flags);
      const uint32_t after=Read<uint32_t>(plan.new_classes[c],layout.access_flags);
      plan.abstract_instances|=(after&0x400u)&&!(before&0x400u);matched=true;break;
    }
    if(!matched)return fail("Structural instance has no Class replacement");
  }
  *output=std::move(result);if(error)error->clear();return true;
}
}
