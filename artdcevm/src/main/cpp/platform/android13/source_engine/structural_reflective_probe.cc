#include "platform/android13/structural_reflective_abi.h"
#include "reflective_value_visitor.h"
#include "mirror/executable.h"
#include "mirror/class-inl.h"
#include "platform/android13/validation_metadata_abi.h"
#include "art_method-inl.h"
#include <cstring>
extern "C" bool ArtDcevmSourceProbeStructuralReflection(
 artdcevm::android13::structural_reflective_abi::SourceView (*read)(const artdcevm::android13::structural_reflective_abi::SourceInfo&),
 artdcevm::android13::structural_reflective_abi::Visitor* visitor) {
 namespace fixed=artdcevm::android13::structural_reflective_abi;
 static_assert(sizeof(art::ReflectionSourceInfo)==sizeof(fixed::SourceInfo));
 static_assert(sizeof(art::HeapReflectiveSourceInfo)==sizeof(fixed::HeapSourceInfo));
 static_assert(art::kSourceJavaLangReflectExecutable==fixed::kExecutable && art::kSourceJniMethodId==fixed::kJniMethod);
 static_assert(art::kSourceDexCacheResolvedMethod==fixed::kDexMethod && art::kSourceDexCacheResolvedField==fixed::kDexField);
 if(art::mirror::Executable::ArtMethodOffset().Int32Value()+sizeof(uint64_t)!=fixed::kExecutableFlags)return false;
 auto* object=reinterpret_cast<art::mirror::Object*>(0x12345600);
 art::HeapReflectiveSourceInfo heap(art::kSourceJavaLangReflectExecutable,object);
 auto h=read(reinterpret_cast<const fixed::SourceInfo&>(heap));
 art::ReflectionSourceInfo dex(art::kSourceDexCacheResolvedField);
 auto d=read(reinterpret_cast<const fixed::SourceInfo&>(dex));
 auto* source_visitor=reinterpret_cast<art::ReflectiveValueVisitor*>(visitor);
 if(source_visitor->VisitMethod(nullptr,dex)!=reinterpret_cast<art::ArtMethod*>(0x123) ||
    source_visitor->VisitField(nullptr,dex)!=reinterpret_cast<art::ArtField*>(0x456))return false;
 return h.type==heap.GetType() && h.object==object && d.type==dex.GetType() && !d.object;
}

extern "C" bool ArtDcevmSourceProbeStructuralMemberMappings(void* raw)
 REQUIRES_SHARED(art::Locks::mutator_lock_) {
 using Fixed=artdcevm::android13::validation_abi::ClassPrefix;
 static_assert(art::kAccPrivate==0x2 && art::kAccStatic==0x8 && art::kAccConstructor==0x10000);
 auto* klass=static_cast<art::mirror::Class*>(raw);
 uint64_t array;uint16_t direct;
 std::memcpy(&array,static_cast<unsigned char*>(raw)+offsetof(Fixed,methods),sizeof(array));
 std::memcpy(&direct,static_cast<unsigned char*>(raw)+offsetof(Fixed,virtual_methods_offset),sizeof(direct));
 auto methods=klass->GetMethods(art::PointerSize::k64);
 if(direct!=klass->GetDirectMethods(art::PointerSize::k64).size())return false;
 uint32_t count=0;if(array)std::memcpy(&count,reinterpret_cast<void*>(array),4);
 return count==methods.size() && (count==0 || reinterpret_cast<unsigned char*>(array)+8==reinterpret_cast<void*>(&*methods.begin()));
}
