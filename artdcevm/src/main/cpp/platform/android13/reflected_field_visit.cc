#include "platform/android13/reflected_field_visit.h"
#include "platform/android13/member_decode.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/structural_reflective_abi.h"
#include "platform/android13/retired_field_registry_bridge.h"
#include "platform/android13/field_copy_metadata_abi.h"
#include "platform/android13/validation_metadata_abi.h"
#include "platform/android13/field_copy_storage.h"
#include <cstring>
#include <cstdlib>
#include <limits>
#include <dlfcn.h>
namespace artdcevm::android13 {
namespace {
const void* heap_source_vptr=nullptr;void* (*root)(void*)=nullptr;
using Field=member_decode_abi::ReflectedField;
using NativeField=field_copy_abi::ArtField;
template<class T>T Read(const void* object,size_t offset){T value;std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,sizeof(value));return value;}
void Store(void* object,size_t offset,uint32_t value){__atomic_store_n(reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(object)+offset),value,__ATOMIC_RELAXED);}
void Require(bool valid){if(!valid)std::abort();}
}
void VisitReflectedFieldTarget(void* field,void* raw_visitor){
 void* original=DecodeReflectedField(field);
 // A real device vtable preserves Describe/RTTI for visitors outside our enhancement.
 structural_reflective_abi::HeapSourceInfo info{{heap_source_vptr,2},field};
 auto* visitor=static_cast<structural_reflective_abi::Visitor*>(raw_visitor);
 void* replacement=visitor->VisitField(original,info.base);Require(replacement!=nullptr);
 uint32_t token=RetiredFieldTokenFor(replacement);
 if(token){Store(field,offsetof(Field,art_field_index),token);return;}
 if(original==replacement)return;
 const uint32_t flags=__atomic_load_n(reinterpret_cast<const uint32_t*>(static_cast<unsigned char*>(replacement)+offsetof(NativeField,access_flags)),__ATOMIC_RELAXED);
 Store(field,offsetof(Field,offset),Read<uint32_t>(replacement,offsetof(NativeField,offset)));
 Store(field,offsetof(Field,access_flags),flags);
 void* declaring=root(replacement);Require(declaring!=nullptr);
 NativeField declaring_slot{};declaring_slot.offset=offsetof(Field,declaring_class);
 Require(reinterpret_cast<uintptr_t>(declaring)<=std::numeric_limits<uint32_t>::max());
 WriteCopyValue(field,&declaring_slot,'L',reinterpret_cast<uintptr_t>(declaring));
 auto* array=reinterpret_cast<void*>(Read<uint64_t>(declaring,(flags&member_decode_abi::kStatic)?offsetof(validation_abi::ClassPrefix,sfields):offsetof(validation_abi::ClassPrefix,ifields)));
 Require(array!=nullptr);
 const uintptr_t begin=reinterpret_cast<uintptr_t>(array)+field_copy_abi::kFieldArrayDataOffset;
 const uintptr_t address=reinterpret_cast<uintptr_t>(replacement);
 Require(address>=begin && (address-begin)%sizeof(NativeField)==0);
 const size_t index=(address-begin)/sizeof(NativeField);Require(index<Read<uint32_t>(array,0));
 Store(field,offsetof(Field,art_field_index),static_cast<uint32_t>(index));
 CopyWriteBarrier(field);
}
bool ConfigureReflectedFieldVisit(HookEngine& hooks,void* implementation,std::string* error){
 auto table=static_cast<const unsigned char*>(hooks.FindSymbol("libart.so","_ZTVN3art24HeapReflectiveSourceInfoE"));
 auto reader=reinterpret_cast<void* (*)(void*)>(hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
 using Probe=bool (*)(const void*);
 auto probe=reinterpret_cast<Probe>(dlsym(implementation,"ArtDcevmSourceProbeReflectedFieldVisit"));
 if(!table||!reader||!probe){*error="Missing named reflected-field visitor ABI functions";return false;}
 root=reader;heap_source_vptr=table+2*sizeof(void*);
 if(!probe(heap_source_vptr)){*error="Reflected-field visitor fixed ABI calibration failed";return false;}
 return true;
}
}
