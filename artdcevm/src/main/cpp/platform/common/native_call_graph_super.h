#pragma once
#include "platform/common/native_call_graph_resolution.h"
#include "platform/common/native_call_graph_dispatch.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_source_stop.h"
#include "platform/abi/arm64/android17/checked_method_resolution.h"
namespace artdcevm::native_call_graph_super {
// Ephemeral source facts, never a rooted census/graph completeness certificate.
// Any later resolver or safepoint invalidates the movable Class addresses.
struct Observation {
 void*caller=nullptr;void*referenced_class=nullptr;void*caller_class=nullptr;
 void*super_class=nullptr;void*declaration=nullptr;void*target=nullptr;
 const void*dex_namespace=nullptr;uint32_t dex_pc=UINT32_MAX;
 uint16_t type_index=0,slot=0;bool interface_super=false;
};
class Resolver final {
 public:
 Resolver(const modern_field_metadata::FieldMetadata&m,const method_metadata::Layout&layout,
 const native_abstract_instances::Service&v):metadata_(m),layout_(layout),vtables_(v){}
 Resolver(const Resolver&)=delete;Resolver&operator=(const Resolver&)=delete;
 // Ordinary native JNI only; reuses the sole original receiver observer.
 bool Bind(HookEngine&,JNIEnv*,std::string*);
 // Actual checked method resolution must already have succeeded. Resolve the
 // *referenced* caller MethodId type independently, not the declaration's owner.
 // This may load/GC and must run before loading is paused. The inventory must
 // immediately map returned Class addresses to its existing actual root slots
 // before any later resolver/JNI, then re-enumerate and revalidate those roots.
 bool ResolveOriginal(const runnable_jni::Context&,receiver_guard::SourceProducer&,
 void*caller,const receiver_guard::CodeView&,const std::vector<unsigned char>&,
 const native_call_graph::CallSite&,void*actual_resolved,
 const modern_field_metadata::ReadContext&,Observation*,std::string*)const;
 // Reader only: the Inventory's private consumer supplies current actual
 // Store.Read roots and the remembered immutable DEX reference. This does not
 // mint a root/census receipt. Interface-super remains explicitly unsupported.
 bool ValidateObserved(const native_source_stop::Session&,void*self,
 const receiver_guard::CodeView&,const std::vector<unsigned char>&,
 const native_call_graph::CallSite&,const evolution::MappedDexMethodReference&,
 const Observation&,const modern_field_metadata::ReadContext&,
 const char**failure=nullptr)const noexcept;
 private:
 using TypeResolver=abi::arm64::android17::ResolveMethodClass;
 using InterfaceSuper=void*(*)(void*,void*,abi::arm64::android17::MethodPointerSize);
 const modern_field_metadata::FieldMetadata&metadata_;method_metadata::Layout layout_;
 const native_abstract_instances::Service&vtables_;
 void*runtime_=nullptr;void**runtime_slot_=nullptr;void*linker_=nullptr;
 TypeResolver resolve_type_=nullptr;InterfaceSuper interface_super_=nullptr;
};
namespace detail {
using InterfaceSelector=void*(*)(void*,void*,abi::arm64::android17::MethodPointerSize);
// Pure ABI forwarding, not an owner or source permission. The concrete
// Resolver obtains original solely from the exact named libart symbol.
inline void* InterfaceTarget(InterfaceSelector original,void*referenced,void*resolved){
 return original&&referenced&&resolved?original(referenced,resolved,abi::arm64::android17::MethodPointerSize::k64):nullptr;
}
inline bool Invokable(const method_metadata::Layout&layout,const void*method,size_t extent,
 bool declaration,method_metadata::Identity*out)noexcept{
 return out&&method_metadata::ReadIdentity(layout,method,extent,out)&&out->dex_method_index!=UINT32_MAX&&
 !(out->access_flags&(declaration?0x5000au:0x5040au));
}
}
}
