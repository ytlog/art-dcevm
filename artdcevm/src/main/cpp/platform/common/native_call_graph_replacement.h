#pragma once
#include "platform/common/native_call_graph_incoming.h"
#include "platform/common/native_owned_removal.h"
#include "platform/common/native_source_class_loading.h"
#include "platform/common/native_receiver_producer.h"
#include "platform/common/native_selective_interpretation.h"

namespace artdcevm::native_call_graph_replacement {
// One replacement Class from the actual loading Run's original Holder arrays.
// Stage independently roots old/new arrays, old/new Class and public loader in
// the caller's operation Store. That Store may also own the BEFORE selection.
// No raw managed object address persists across JNI/GC. Captured native methods
// remain owned by the rooted replacement Class's actual array.
class Binding final {
 public:
 Binding()=default;Binding(const Binding&)=delete;Binding&operator=(const Binding&)=delete;
 bool Stage(HookEngine&,const runnable_jni::Context&,jvmtiEnv*,
            const native_source_class_loading::Run&,const void*driver,
            const native_owned_removal::RootedGraph&,size_t partition,size_t class_index,
            const native_call_graph_incoming::Inventory&,
            bounded_managed_roots::Store&,const modern_field_metadata::FieldMetadata&,
            const method_identity::Calibration&,const receiver_guard::SourceProducer&,std::string*);
 // Allocation/JNI/OS-discovery free. Each successful validation requires the
 // actual stop, same live loading Run/Holder, original rooted array slot,
 // unchanged Store generation and complete current incoming DEX/method array.
 // This is method/root identity only: call resolution, union closure, stack
 // drain, native boundaries, GC references and publication remain independent.
 bool Validate(const native_source_stop::Session&,const modern_field_metadata::ReadContext&)const noexcept;
 const std::vector<native_selective_interpretation::Method>&Selection()const noexcept{return prepared_?selection_:empty_;}
 size_t DeclaredCount()const noexcept{return prepared_?declared_.size():0;}
 private:
 struct Declared {void*method=nullptr;uint32_t index=UINT32_MAX,flags=0;bool body=false;};
 bool Check(const modern_field_metadata::ReadContext&)const noexcept;
 bool HolderArrays(const modern_field_metadata::ReadContext&)const noexcept;
 const native_call_graph_incoming::Inventory*incoming_=nullptr;
 const bounded_managed_roots::Store*roots_=nullptr;uint64_t generation_=0;
 const modern_field_metadata::FieldMetadata*metadata_=nullptr;
 const receiver_guard::SourceProducer*source_=nullptr;
 const native_source_class_loading::Run*loading_=nullptr;
 const void*driver_=nullptr;const void*holder_=nullptr;void*self_=nullptr;size_t owner_count_=0,holder_index_=0,class_index_=0,class_count_=0;
 bounded_managed_roots::Slot old_array_{},new_array_{},old_class_{},new_class_{},loader_{};
 using RootBarrier=void*(*)(void*);RootBarrier root_=nullptr;
 method_metadata::Layout layout_{};void*array_=nullptr;
 std::vector<Declared>declared_;std::vector<native_selective_interpretation::Method>selection_;const std::vector<native_selective_interpretation::Method>empty_{};
 bool attempted_=false,prepared_=false;
};
}
