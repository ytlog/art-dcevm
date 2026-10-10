#pragma once
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/modern_field_metadata.h"
namespace artdcevm::native_hierarchy_graph {
// One rooted identity path for complete graph owners. The proxy name is an
// independently observed public String, registered alongside Class/loader in
// the operation root pool; its managed value is refreshed by the same resolver.
// This does not establish an allocator/publication owner for a replacement proxy.
inline bool ClassIdentity(const modern_field_metadata::FieldMetadata&m,const Node&n,
 void*klass,field_copy::CopyClassView*out,std::string*error=nullptr,
 const modern_field_metadata::ReadContext*context=nullptr){
 size_t extent=0;class_metadata::State state;
 const auto&layout=m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 if(!modern_field_metadata::ReadMappingExtent(context,klass,&extent)||!class_metadata::ReadState(layout,klass,extent,&state)||state.IsPrimitive())return false;
 const bool proxy=state.access_flags&0x40000u;
 if(proxy!=bool(n.proxy_name_root.owner))return false;
 if(!proxy)return m.ClassIdentity(klass,out,error,context);
 if(!n.proxy_name_root.Valid()||n.proxy_name_text.empty()||n.negative_proxy_name.empty())return false;
 void*name=n.proxy_name;
 // Ordinary source resolution may safepoint. Independently reborrow the exact
 // public Class/name slots after that callee; stopped readers consume only the
 // already registered/refreshed values, with no global decoder or JNI.
 if(!context){void*rooted=nullptr;if(!n.class_root.Valid()||!n.class_root.owner->Read(n.class_root.slot,&rooted)||rooted!=klass||!n.proxy_name_root.owner->Read(n.proxy_name_root.slot,&name))return false;}
 else if(klass!=n.klass)return false;
 if(!name)return false;
 return m.ClassIdentityVerified(klass,n.descriptor.c_str(),name,n.proxy_name_text.c_str(),n.negative_proxy_name.c_str(),out,error,context);
}
}
