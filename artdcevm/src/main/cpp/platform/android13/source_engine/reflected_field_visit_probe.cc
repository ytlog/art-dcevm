#include "platform/android13/structural_reflective_abi.h"
#include "reflective_value_visitor.h"
#include "mirror/field.h"
#include "obj_ptr-inl.h"
#include <cstring>
extern "C" bool ArtDcevmSourceProbeReflectedFieldVisit(const void* vptr){
 using Fixed=artdcevm::android13::structural_reflective_abi::HeapSourceInfo;
 static_assert(sizeof(art::HeapReflectiveSourceInfo)==sizeof(Fixed));
 static_assert(art::kSourceJavaLangReflectField==2);
 auto* object=reinterpret_cast<art::mirror::Object*>(0x12345600);
 art::HeapReflectiveSourceInfo info(art::kSourceJavaLangReflectField,object);
 const void* expected;std::memcpy(&expected,static_cast<const void*>(&info),sizeof(expected));
 if(expected!=vptr)return false;
 void* actual_object;std::memcpy(&actual_object,reinterpret_cast<const unsigned char*>(&info)+offsetof(Fixed,object),sizeof(actual_object));
 if(actual_object!=object)return false;
 Fixed fixed{{vptr,2},object};
 const auto& source=*reinterpret_cast<const art::HeapReflectiveSourceInfo*>(&fixed);
 if(source.GetType()!=info.GetType())return false;
 return true;
}
