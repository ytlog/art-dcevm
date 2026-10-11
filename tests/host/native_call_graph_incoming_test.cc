#include "platform/common/native_call_graph_incoming.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace artdcevm;
std::vector<unsigned char> File(const char*path){std::ifstream in(path,std::ios::binary);assert(in);return {std::istreambuf_iterator<char>(in),{}};}
template<class T>void Put(std::vector<unsigned char>&bytes,size_t at,T value){assert(at+sizeof(T)<=bytes.size());std::memcpy(bytes.data()+at,&value,sizeof(value));}
void Checksum(std::vector<unsigned char>&bytes){uint32_t a=1,b=0;for(size_t i=12;i<bytes.size();++i){a=(a+bytes[i])%65521;b=(b+a)%65521;}Put(bytes,8,(b<<16)|a);}
receiver_guard::CodeView Relocate(receiver_guard::CodeView code,const std::vector<unsigned char>&copy){
 const auto offset=static_cast<const unsigned char*>(code.code_item)-code.source_dex.begin;
 code.source_dex={copy.data(),copy.size(),copy.data(),copy.size(),code.source_dex.class_def_index};
 code.code_item=copy.data()+offset;code.instructions=reinterpret_cast<const uint16_t*>(copy.data()+offset+16);return code;
}
int main(int argc,char**argv){
 assert(argc==4);auto bytes=File(argv[1]);std::string error;
 native_call_graph_incoming::Inventory inventory;
 assert(inventory.Capture(bytes,0,"LFixture;",&error)&&error.empty());
 assert(inventory.Schema()->descriptor=="LFixture;"&&inventory.Methods().size()==3);
 const native_call_graph_incoming::Method*caller=nullptr;
 for(const auto&m:inventory.Methods()){
  if(m.declaration.name=="gone"){assert(!m.code_offset&&!m.direct&&m.calls.empty());}
  if(m.declaration.name=="keep"){caller=&m;assert(!m.direct&&m.calls.size()==1);}
  if(m.declaration.name=="staticKeep")assert(m.direct&&m.code_offset&&m.calls.empty());
 }
 assert(caller&&caller->calls[0].site.dex_pc==0&&caller->calls[0].reference.declaring_class=="LFixture;"&&
        caller->calls[0].reference.name=="staticKeep"&&caller->calls[0].reference.descriptor=="()I");
 receiver_guard::CodeView original;assert(inventory.Code(1,&original)&&inventory.MatchesCode(1,original));
 receiver_guard::CodeView sentinel=original;
 assert(!inventory.Code(0,&sentinel)&&sentinel.code_item==original.code_item);
 assert(!inventory.Code(999,&sentinel)&&sentinel.code_item==original.code_item);
 // Incoming copy is independent of the caller-owned request buffer. A fresh
 // same-content runtime mapping may relocate but must preserve original offsets.
 auto relocated=bytes;auto actual=Relocate(original,relocated);assert(inventory.MatchesCode(1,actual));
 bytes[0]=0;assert(inventory.MatchesCode(1,original));
 auto damaged=actual;damaged.instructions++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.code_item=static_cast<const unsigned char*>(actual.code_item)+4;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.units++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.registers++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.ins_size++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.tries_size++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.method_ids++;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.compact=true;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.source_dex.class_def_index=1;assert(!inventory.MatchesCode(1,damaged));
 damaged=actual;damaged.source_dex.size--;assert(!inventory.MatchesCode(1,damaged));
 const size_t offset=static_cast<const unsigned char*>(actual.code_item)-relocated.data();
 relocated[offset+16]^=1;assert(!inventory.MatchesCode(1,actual));relocated[offset+16]^=1;
 // A genuinely changed incoming call targets a different MethodId/owner;
 // nothing in the BEFORE inventory can supply this new edge.
 auto incoming=relocated;Put<uint16_t>(incoming,offset+16,0x106e);Put<uint16_t>(incoming,offset+18,3);Checksum(incoming);
 native_call_graph_incoming::Inventory changed;
 assert(changed.Capture(incoming,0,"LFixture;",&error));
 for(const auto&m:changed.Methods())if(m.declaration.name=="keep"){
  assert(m.calls.size()==1&&m.calls[0].site.instruction.kind==receiver_guard::InvokeKind::Virtual&&
         m.calls[0].reference.declaring_class=="Ljava/lang/Object;"&&m.calls[0].reference.name=="keep");
 }
 // Selecting a second ClassDef must retain its own declaration/code identity.
 native_call_graph_incoming::Inventory parent;assert(parent.Capture(relocated,1,"Ljava/lang/Object;",&error));
 assert(parent.Methods().size()==1&&parent.Methods()[0].declaration.dex_index==3);
 receiver_guard::CodeView parent_code;assert(parent.Code(3,&parent_code)&&parent.MatchesCode(3,parent_code));
 assert(!parent.MatchesCode(1,actual));
 assert(!inventory.Capture(incoming,0,"LFixture;",&error)&&inventory.MatchesCode(1,original));
 const auto refused=[&](std::vector<unsigned char>input,uint32_t klass,const char*name){
  native_call_graph_incoming::Inventory rejected;assert(!rejected.Capture(input,klass,name,&error));
  assert(!error.empty()&&!rejected.Schema()&&rejected.Methods().empty());
 };
 refused(bytes,0,"LFixture;");refused(relocated,2,"LFixture;");refused(relocated,0,"LOther;");
 auto bad=relocated;Put<uint16_t>(bad,offset+2,2);Checksum(bad);refused(bad,0,"LFixture;");
 bad=relocated;Put<uint32_t>(bad,offset+12,UINT32_MAX);Checksum(bad);refused(bad,0,"LFixture;");
 bad=relocated;Put<uint16_t>(bad,offset+18,UINT16_MAX);Checksum(bad);refused(bad,0,"LFixture;");
 bad=relocated;Put<uint16_t>(bad,offset+16,0x10fa);Checksum(bad);refused(bad,0,"LFixture;");
 bad=relocated;Put<uint16_t>(bad,offset+16,0x10fc);Checksum(bad);refused(bad,0,"LFixture;");
 // The shared declaration reader must preserve both outputs on format errors.
 evolution::ClassSchema schema;schema.descriptor="sentinel";
 std::vector<evolution::DexMethodCode>codes{{123,456,true}};
 assert(!evolution::ReadDexClassCode(bytes,0,&schema,&codes,&error));
 assert(schema.descriptor=="sentinel"&&codes.size()==1&&codes[0].dex_index==123&&codes[0].code_offset==456);
 // Single-file v041 shares the same incoming inventory and relocation rules.
 native_call_graph_incoming::Inventory container;auto v041=File(argv[2]);
 assert(container.Capture(v041,0,"Ltest/ContainerSubject;",&error)&&container.Methods().size()==1);
 receiver_guard::CodeView container_code;assert(container.Code(0,&container_code)&&container.MatchesCode(0,container_code));
 // Added method + its new downstream call are present in incoming class_data,
 // even though BEFORE had no declaration/body for newKeep at any MethodId.
 native_call_graph_incoming::Inventory added;auto extra=File(argv[3]);
 assert(added.Capture(extra,0,"LFixture;",&error)&&added.Methods().size()==4);
 bool saw_new=false;
 for(const auto&m:added.Methods())if(m.declaration.name=="newKeep"){
  saw_new=true;assert(m.direct&&m.declaration.dex_index==2&&m.calls.size()==1&&
   m.calls[0].reference.name=="staticKeep"&&m.calls[0].site.instruction.index==3);
  receiver_guard::CodeView new_code;assert(added.Code(2,&new_code)&&added.MatchesCode(2,new_code));
 }
 assert(saw_new);
 std::cout<<"Actual incoming ClassDef/encoded methods/new call targets, owned bytes, runtime relocation/geometry refusal and v041 PASS; no replacement roots or commit permission\n";
}
