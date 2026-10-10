#pragma once
#include "platform/abi/arm64/android11/native_offset.h"
#include <atomic>
#include <cstdint>
namespace artdcevm::abi::arm64::android11::initializer_transaction {
// AOSP Offset/MemberOffset is a native word, not an int32 ABI packet.
using MemberOffset=NativeOffset;
using ActiveTransaction=bool(*)(const void*);
using RecordWrite32=void(*)(void*,void*,MemberOffset,uint32_t,bool);
using SetClinitTid=void(*)(void*,int32_t);
inline constexpr const char* kActive="_ZNK3art7Runtime19IsActiveTransactionEv";
inline constexpr const char* kRecordConst="_ZNK3art7Runtime18RecordWriteField32EPNS_6mirror6ObjectENS_12MemberOffsetEjb";
inline constexpr const char* kRecord="_ZN3art7Runtime18RecordWriteField32EPNS_6mirror6ObjectENS_12MemberOffsetEjb";
inline constexpr const char* kSetTid="_ZN3art6mirror5Class17SetClinitThreadIdEi";
// Fixed source Atomic<int32_t>::LoadJavaData/StoreJavaData, Object::
// Get/SetFieldPrimitive<int32_t,false>, SetField32Transaction<true,...,false>.
// The call owner must prove a valid rooted Class and original mutator/monitor
// window before using this declaration. This is not a permission object.
struct Atomic32:std::atomic<int32_t>{
 int32_t LoadJavaData()const{return load(std::memory_order_relaxed);}
 void StoreJavaData(int32_t value){store(value,std::memory_order_relaxed);}
};
static_assert(sizeof(Atomic32)==4&&alignof(Atomic32)==4);
inline void SetClinitThreadId(void* runtime,void* object,MemberOffset offset,int32_t tid,
                             ActiveTransaction active,RecordWrite32 record){
 const bool transaction=active(runtime);
 auto* cell=reinterpret_cast<Atomic32*>(static_cast<unsigned char*>(object)+offset.value);
 // Original transaction branch records the old Java-data value before storing
 // the new one. A failing recorder therefore cannot write the destination.
 if(transaction)record(runtime,object,offset,uint32_t(cell->LoadJavaData()),false);
 cell->StoreJavaData(tid);
}
}
