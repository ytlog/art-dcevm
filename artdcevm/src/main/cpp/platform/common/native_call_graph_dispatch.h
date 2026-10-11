#pragma once
#include "platform/common/native_call_graph.h"
#include "platform/common/native_abstract_instances.h"
#include <cstring>
namespace artdcevm::native_call_graph_dispatch {
struct Observation {void*declaration=nullptr;void*declaring_class=nullptr;void*receiver_class=nullptr;void*target=nullptr;uint16_t slot=0;};
namespace detail {
inline bool Slot(const void*storage,size_t extent,size_t payload,size_t count,size_t slot,void**out)noexcept{
 if(!out||!storage||payload>extent||count>(extent-payload)/8||slot>=count)return false;
 uint64_t address=0;std::memcpy(&address,static_cast<const unsigned char*>(storage)+payload+slot*8,8);
 if(!address||address%4)return false;*out=reinterpret_cast<void*>(uintptr_t(address));return true;
}
inline bool MethodSlot(const method_metadata::Layout&layout,const void*method,size_t extent,uint16_t*out)noexcept{
 const size_t offset=layout.size==sizeof(method_metadata::Method11)?offsetof(method_metadata::Method11,method_index):offsetof(method_metadata::Method12,method_index);
 if(!out||!method||(layout.size!=sizeof(method_metadata::Method11)&&layout.size!=sizeof(method_metadata::Method12))||extent<layout.size||offset>extent||extent-offset<2)return false;
 std::memcpy(out,static_cast<const unsigned char*>(method)+offset,2);return true;
}
}
// Ephemeral no-safepoint native observations only. Caller independently owns
// roots, same-generation complete class census and actual domain membership:
// virtual declaring-Class assignability or interface membership. This reader
// neither constructs that domain nor authorizes interpretation/publication.
class Reader final {
 public:
 Reader(const modern_field_metadata::FieldMetadata&m,const native_abstract_instances::Service&v,
 const method_metadata::Layout&layout):metadata_(m),vtables_(v),layout_(layout){}
 bool Observe(native_call_graph::InvokeKind,void*actual_resolved_declaration,void*current_receiver_class,
 const modern_field_metadata::ReadContext&,Observation*,const char**failure=nullptr)const noexcept;
 private:
 const modern_field_metadata::FieldMetadata&metadata_;const native_abstract_instances::Service&vtables_;
 method_metadata::Layout layout_;
};
}
