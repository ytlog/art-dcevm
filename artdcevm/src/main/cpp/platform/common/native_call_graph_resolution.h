#pragma once
#include "platform/common/native_call_graph_code.h"
#include "platform/common/runnable_jni_bridge.h"
#include <cstring>
namespace artdcevm::receiver_guard { class SourceProducer; }
namespace artdcevm::native_call_graph_resolution {
enum class Status:uint8_t {Unavailable,Resolved,OriginalException};
struct Result {
 Status status=Status::Unavailable;void* caller=nullptr;void* declaration=nullptr;
 uint32_t dex_pc=UINT32_MAX;receiver_guard::InvokeInstruction instruction{};
 const void* dex_namespace=nullptr;
};
// May suspend through the original ART resolver. The fresh inventory owner must
// root and remap the returned native declaration before building dispatch edges.
Result ResolveOriginal(const runnable_jni::Context&,receiver_guard::SourceProducer&,
 void*caller,const receiver_guard::CodeView&,const std::vector<unsigned char>&,
 const native_call_graph::CallSite&);
namespace detail {
// Source mechanics only. This testable leaf grants no dispatch completeness,
// root lifetime or interpreter activation capability.
inline bool Same(const receiver_guard::InvokeInstruction&a,const receiver_guard::InvokeInstruction&b)noexcept{
 return a.kind==b.kind&&a.index==b.index&&a.receiver_register==b.receiver_register&&
 a.argument_count==b.argument_count&&a.range==b.range;
}
inline bool Fresh(const receiver_guard::CodeView&actual,const receiver_guard::CodeView&staged,
 const std::vector<unsigned char>&bytes,const native_call_graph::CallSite&site){
 if(!actual.instructions||!actual.source_dex.begin||actual.code_item!=staged.code_item||
 actual.source_dex.begin!=staged.source_dex.begin||actual.units!=staged.units||
 actual.registers!=staged.registers||actual.method_ids!=staged.method_ids||
 actual.units>SIZE_MAX/2||bytes.size()!=actual.units*2||
 std::memcmp(bytes.data(),actual.instructions,bytes.size()))return false;
 std::vector<native_call_graph::CallSite>sites;
 if(!native_call_graph::ScanCode(actual,&sites))return false;
 for(const auto&s:sites)if(s.dex_pc==site.dex_pc)return Same(s.instruction,site.instruction);
 return false;
}
inline Result Resolve(receiver_guard::BytecodeAccess access,void*self,void*caller,
 const receiver_guard::CodeView&staged,const std::vector<unsigned char>&bytes,
 const native_call_graph::CallSite&site,void*exception_owner,bool(*exception)(void*)noexcept){
 Result result{};
 if(!self||!caller||!access.current||!access.code||!access.resolve||!exception||
 !access.current(access.owner,self)||exception(exception_owner)||
 site.instruction.kind>receiver_guard::InvokeKind::Interface)return result;
 receiver_guard::CodeView current{};
 if(!access.code(access.owner,self,caller,&current)||!Fresh(current,staged,bytes,site))return result;
 void*target=nullptr;void*special=nullptr;
 const bool resolved=access.resolve(access.owner,self,caller,site.dex_pc,site.instruction,&target,&special);
 // Keep the original exception pending; it is an unresolved graph edge, never
 // permission to use a partial target. Callers must stop this resolving batch.
 if(exception(exception_owner)){result.status=Status::OriginalException;return result;}
 if(!resolved||!target||!access.current(access.owner,self)||
 !access.code(access.owner,self,caller,&current)||!Fresh(current,staged,bytes,site))return result;
 result={Status::Resolved,caller,target,site.dex_pc,site.instruction,current.source_dex.begin};
 return result;
}
}
}
