#pragma once
#include "platform/common/native_interface_dispatch_domain.h"
namespace interface_domain_fixture {
inline bool RefreshMaps(void*raw,std::string*error){
 if(!BootRefreshMaps(raw,error))return false;auto&maps=*static_cast<RunStateMappings*>(raw);
 for(const auto&method:whole_inline_fixture::methods)maps.Cover(const_cast<unsigned char*>(method.bytes->data()));
 for(void*klass:owned_classes){void*cache=Root(static_cast<char*>(klass)+offsetof(class_metadata::Class17,dex_cache));if(!cache)continue;void*file=nullptr;std::memcpy(&file,static_cast<char*>(cache)+16,8);if(file)maps.Cover(const_cast<unsigned char*>(static_cast<dex_mapping_abi::Prefix*>(file)->begin));}
 return true;
}
inline void TestFinalDomain(native_schema_run_state::RunState&run,const modern_field_metadata::ReadContext&read){
 const auto*source=run.InterfaceDomain();auto*receipt=run.InterfaceDomainCertificate();assert(source&&receipt);
 const char*failure=nullptr;const auto os=method_identity::MappingOsQueries();
 if(!source->Observe(whole_inline_fixture::caller,0,read,receipt,&failure)){std::fprintf(stderr,"Domain Observe: %s\n",failure?failure:"unknown");std::abort();}
 assert(receipt->Caller()==whole_inline_fixture::caller&&receipt->DexPc()==0&&receipt->Declaration());
 assert(source->Validate(*receipt,read,&failure));
 // Stopped receipt revalidation uses the exact independent current caller,
 // declaration and namespace. No receiver or native-return permission exists.
 auto*method=static_cast<abi::arm64::android12::Method12*>(whole_inline_fixture::caller);const auto index=method->dex_method_index;
 method->dex_method_index=UINT32_MAX;assert(!source->Validate(*receipt,read,&failure));method->dex_method_index=index;
 assert(source->Validate(*receipt,read,&failure));
 assert(!source->Observe(whole_inline_fixture::caller,1,read,receipt,&failure));
 assert(!receipt->Caller()&&!source->Validate(*receipt,read,&failure));
 if(!source->Observe(whole_inline_fixture::caller,0,read,receipt,&failure)){std::fprintf(stderr,"Domain Observe: %s\n",failure?failure:"unknown");std::abort();}
 // Corrupt actual affected IfTable: the readonly census/graph certificate must
 // refuse even when independent interface declaration/caller are unchanged.
 void*affected=old_class;const auto table=Read32(affected,offsetof(class_metadata::Class17,iftable));Put32(affected,offsetof(class_metadata::Class17,iftable),0);
 assert(!source->Validate(*receipt,read,&failure));Put32(affected,offsetof(class_metadata::Class17,iftable),table);
 assert(source->Validate(*receipt,read,&failure));
 // The complete readonly census includes ERROR classes and array marker roots;
 // immutable interface declaration identity is independent of these dependencies.
 auto*decl=static_cast<abi::arm64::android12::Method12*>(receipt->Declaration());const auto flags=decl->access_flags;
 decl->access_flags|=8;assert(!source->Validate(*receipt,read,&failure));decl->access_flags=flags;
 assert(source->Validate(*receipt,read,&failure));
 const auto err_status=Read32(erroneous,offsetof(class_metadata::Class17,status));
 Put32(erroneous,offsetof(class_metadata::Class17,status),15u<<28);assert(!source->Validate(*receipt,read,&failure));
 Put32(erroneous,offsetof(class_metadata::Class17,status),err_status);assert(source->Validate(*receipt,read,&failure));
 void*array=BootLongArrayClass();const auto array_table=Read32(array,offsetof(class_metadata::Class17,iftable));
 Put32(array,offsetof(class_metadata::Class17,iftable),table);assert(!source->Validate(*receipt,read,&failure));
 Put32(array,offsetof(class_metadata::Class17,iftable),array_table);assert(source->Validate(*receipt,read,&failure));
 const auto marker_loader=Read32(cloneable,offsetof(class_metadata::Class17,class_loader));
 Put32(cloneable,offsetof(class_metadata::Class17,class_loader),uint32_t(uintptr_t(guard_expected_loader)));assert(!source->Validate(*receipt,read,&failure));
 Put32(cloneable,offsetof(class_metadata::Class17,class_loader),marker_loader);assert(source->Validate(*receipt,read,&failure));
 assert(method_identity::MappingOsQueries()==os);
}
}
