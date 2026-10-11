#include "platform/common/native_call_graph_incoming.h"
#include <cstring>

namespace artdcevm::native_call_graph_incoming {
namespace {
template<class T>T Read(const unsigned char*p){T value;std::memcpy(&value,p,sizeof(value));return value;}
bool View(const std::vector<unsigned char>&bytes,uint32_t klass,const Method&method,
          receiver_guard::CodeView*out)noexcept{
 if(!out||!method.code_offset||bytes.size()<112)return false;
 const size_t offset=method.code_offset;
 if(offset%4||offset>bytes.size()||bytes.size()-offset<16)return false;
 const auto*item=bytes.data()+offset;
 const auto registers=Read<uint16_t>(item),incoming=Read<uint16_t>(item+2);
 const auto units=Read<uint32_t>(item+12);
 if(!units||incoming>registers||uint64_t(units)*2>bytes.size()-offset-16)return false;
 *out={reinterpret_cast<const uint16_t*>(item+16),units,registers,Read<uint32_t>(bytes.data()+88),0,
       {bytes.data(),bytes.size(),bytes.data(),bytes.size(),klass},item,incoming,Read<uint16_t>(item+6),false};
 return true;
}
}
struct Inventory::State {
 std::vector<unsigned char> bytes;
 uint32_t class_def_index=UINT32_MAX;
 evolution::ClassSchema schema;
 std::vector<Method> methods;
};
Inventory::Inventory()=default;
Inventory::~Inventory()=default;
const evolution::ClassSchema*Inventory::Schema()const noexcept{return state_?&state_->schema:nullptr;}
const std::vector<Method>&Inventory::Methods()const noexcept{
 static const std::vector<Method>empty;return state_?state_->methods:empty;
}
bool Inventory::Capture(const std::vector<unsigned char>&bytes,uint32_t klass,
 const std::string&expected,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(state_||!error||expected.empty())return deny("call-graph-incoming-invalid-or-repeated-capture");
 auto candidate=std::make_unique<State>();candidate->bytes=bytes;candidate->class_def_index=klass;
 std::vector<evolution::DexMethodCode>codes;
 if(!evolution::ReadDexClassCode(candidate->bytes,klass,&candidate->schema,&codes,error))return false;
 if(candidate->schema.descriptor!=expected||codes.size()!=candidate->schema.methods.size())
  return deny("call-graph-incoming-ClassDef-or-method-partition-mismatch");
 candidate->methods.reserve(codes.size());
 for(size_t i=0;i<codes.size();++i){
  Method method{candidate->schema.methods[i],codes[i].code_offset,codes[i].direct,{}};
  if(method.declaration.dex_index!=codes[i].dex_index)return deny("call-graph-incoming-method-index-mismatch");
  if(method.code_offset){
   receiver_guard::CodeView code;
   if(!View(candidate->bytes,klass,method,&code))return deny("call-graph-incoming-CodeItem-geometry-refused");
   std::vector<native_call_graph::CallSite>sites;const char*failure=nullptr;
   if(!native_call_graph::ScanCode(code,&sites,&failure))return deny(failure?failure:"call-graph-incoming-callsite-scan-refused");
   for(const auto&site:sites){
    if(site.instruction.kind==receiver_guard::InvokeKind::Custom||site.instruction.kind==receiver_guard::InvokeKind::Polymorphic)
     return deny("call-graph-incoming-custom-or-polymorphic-bridge-unproved");
    Call call;call.site=site;
    if(!evolution::ReadMappedDexMethodReference(code.source_dex,site.instruction.index,&call.reference,error))return false;
    method.calls.push_back(std::move(call));
   }
  }
  candidate->methods.push_back(std::move(method));
 }
 state_=std::move(candidate);error->clear();return true;
}
bool Inventory::Code(uint32_t index,receiver_guard::CodeView*out)const noexcept{
 if(!state_||!out)return false;
 for(const auto&m:state_->methods)if(m.declaration.dex_index==index)
  return View(state_->bytes,state_->class_def_index,m,out);
 return false;
}
bool Inventory::MatchesNamespace(const evolution::MappedDexMetadata&map)const noexcept{
 if(!state_||!map.begin||!map.data_begin||map.class_def_index!=state_->class_def_index||
    map.begin!=map.data_begin||map.size!=state_->bytes.size()||map.data_size<map.size)return false;
 return !std::memcmp(map.begin,state_->bytes.data(),map.size);
}
bool Inventory::MatchesCode(uint32_t index,const receiver_guard::CodeView&actual)const noexcept{
 receiver_guard::CodeView expected;
 if(!Code(index,&expected)||!MatchesNamespace(actual.source_dex)||actual.compact)return false;
 const auto offset=static_cast<const unsigned char*>(expected.code_item)-state_->bytes.data();
 const auto*item=actual.source_dex.data_begin+offset;
 return actual.code_item==item&&actual.instructions==reinterpret_cast<const uint16_t*>(item+16)&&
        actual.units==expected.units&&actual.registers==expected.registers&&actual.ins_size==expected.ins_size&&
        actual.tries_size==expected.tries_size&&actual.method_ids==expected.method_ids&&actual.call_sites==expected.call_sites;
}
}
