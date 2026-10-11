#pragma once
#include <type_traits>
// Exercises the concrete Source with independently prepared public catalog,
// actual native method/DEX metadata, real Pool roots and calibrated Session.
inline void TestSourceSuffixReceipt(artdcevm::native_boot_aot_unaffected::Source&source,
 const source_commit::Plan&plan,const native_source_stop::Session&stop,
 const native_boot_method_catalog::Rooted&catalog,const source_handle_roots::StoppedReader&reader,
 const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&model,
 const native_abstract_instances::Service&vtables,const receiver_guard::SourceProducer&producer,
 const modern_field_metadata::ReadContext&read,whole_process_frames::Service::Prepared&storage,
 void*method,void*relocated_dex){
 using namespace artdcevm::native_boot_aot_unaffected;
 static_assert(!std::is_copy_constructible_v<SuffixReceipt> && !std::is_move_constructible_v<SuffixReceipt>);
 native_boot_return_continuation::HandlerStorage handlers{storage.tries.data(),storage.tries.size(),storage.handlers.data(),storage.handlers.size()};
 native_boot_return_continuation::Scratch scratch{storage.boundaries.data(),storage.seen.data(),storage.queue.data(),storage.queue.size()};
 const auto*entry=catalog.Lookup(method,reader,read);assert(entry&&entry->body==native_boot_method_catalog::Body::kUnknown);
 const char*failure=nullptr;SuffixReceipt empty,receipt;
 assert(!source.ValidateSuffix(empty,read,handlers,scratch,&failure)&&!std::strcmp(failure,"boot-suffix-receipt-source-window-unproved"));
 assert(source.ObserveSuffix(method,0,read,handlers,scratch,&receipt,&failure));
 assert(receipt.Method()==method&&receipt.DexPc()==0&&receipt.Code().code_item&&receipt.Shape().complete&&receipt.Shape().primitive_return&&receipt.Shape().exception_escape);
 assert(source.ValidateSuffix(receipt,read,handlers,scratch,&failure));
 Source foreign(plan,stop,thread.data(),catalog,reader,metadata,model,vtables,producer);
 assert(!foreign.ValidateSuffix(receipt,read,handlers,scratch,&failure)&&!std::strcmp(failure,"boot-suffix-receipt-source-window-unproved"));
 // Real native source CodeItem relocation with equal immutable instructions is
 // still a different receipt binding, not an authorization supplied by bytes.
 auto*native=static_cast<abi::arm64::android12::Method12*>(method);const auto data=native->data;
 void*declaring=metadata.MethodDeclaringClass(method);void*cache=Root(static_cast<char*>(declaring)+offsetof(class_metadata::Class17,dex_cache));uintptr_t file=0;std::memcpy(&file,static_cast<char*>(cache)+16,8);auto*prefix=reinterpret_cast<dex_mapping_abi::Prefix*>(file);const auto original=*prefix;
 native->data=uintptr_t(relocated_dex)+(data-uintptr_t(original.begin));prefix->begin=static_cast<const unsigned char*>(relocated_dex);prefix->data_begin=prefix->begin;
 assert(!source.ValidateSuffix(receipt,read,handlers,scratch,&failure)&&!std::strcmp(failure,"boot-suffix-receipt-CodeItem-or-namespace-changed"));
 *prefix=original;native->data=data;assert(source.ValidateSuffix(receipt,read,handlers,scratch,&failure));
 const auto index=native->dex_method_index;native->dex_method_index=UINT32_MAX;
 assert(!source.ValidateSuffix(receipt,read,handlers,scratch,&failure));native->dex_method_index=index;
 assert(source.ValidateSuffix(receipt,read,handlers,scratch,&failure));
 // A failed new observation revokes its previous receipt, even after source
 // metadata is restored. PC1 is an invoke operand, not an instruction boundary.
 assert(!source.ObserveSuffix(method,1,read,handlers,scratch,&receipt,&failure));
 assert(!receipt.Method()&&!source.ValidateSuffix(receipt,read,handlers,scratch,&failure));
}
