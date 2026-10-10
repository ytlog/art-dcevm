#include "platform/common/source_cache_role_catalog.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <vector>
using artdcevm::source_cache_role_catalog::Catalog;
using artdcevm::source_cache_role_catalog::Read;
static long fail_after=-1;
void* operator new(std::size_t size) {
  if (fail_after==0) throw std::bad_alloc();
  if (fail_after>0) --fail_after;
  if (void* p=std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
namespace {
using Bytes=std::vector<unsigned char>;
void U32(Bytes& b,size_t at,uint32_t v) {
  for (unsigned i=0;i<4;++i) b.at(at+i)=static_cast<unsigned char>(v>>(8*i));
}
void U16(Bytes& b,size_t at,uint16_t v) { b.at(at)=v&255; b.at(at+1)=v>>8; }
void Leb(Bytes& b,uint32_t v) {
  do { const uint8_t n=v&127; v>>=7; b.push_back(n|(v ? 128 : 0)); } while(v);
}
void Align(Bytes& b) { while(b.size()%4) b.push_back(0); }
struct Fixture {
  Bytes b; uint32_t strings=112,types,protos,fields,methods,classes,data;
  uint32_t class_data=0,list=0,string_data=0;
  Fixture() {
    const std::vector<std::string> names={"I","V","LA;","Ljava/lang/Object;","[I","[LA;",
      "integer","external","invoke","clone","VI","L"};
    types=strings+names.size()*4; protos=types+6*4; fields=protos+2*12;
    methods=fields+2*8; classes=methods+2*8; data=classes+32;
    b.resize(data);
    const char magic[]="dex\n039"; std::copy(magic,magic+8,b.begin());
    U32(b,36,112); U32(b,40,0x12345678);
    const uint32_t counts[]={uint32_t(names.size()),6,2,2,2,1};
    const uint32_t offsets[]={strings,types,protos,fields,methods,classes};
    for(unsigned i=0;i<6;++i) { U32(b,56+i*8,counts[i]); U32(b,60+i*8,offsets[i]); }
    for(size_t i=0;i<names.size();++i) {
      U32(b,strings+i*4,b.size()); if(i==0)string_data=b.size();
      Leb(b,names[i].size()); b.insert(b.end(),names[i].begin(),names[i].end()); b.push_back(0);
    }
    for(unsigned i=0;i<6;++i) U32(b,types+i*4,i);
    Align(b); list=b.size(); b.resize(b.size()+6); U32(b,list,1); U16(b,list+4,0);
    U32(b,protos,10); U32(b,protos+4,1); U32(b,protos+8,list);
    U32(b,protos+12,11); U32(b,protos+16,3);
    U16(b,fields,2); U16(b,fields+2,0); U32(b,fields+4,6);
    U16(b,fields+8,3); U16(b,fields+10,4); U32(b,fields+12,7);
    U16(b,methods,2); U16(b,methods+2,0); U32(b,methods+4,8);
    U16(b,methods+8,4); U16(b,methods+10,1); U32(b,methods+12,9);
    class_data=b.size();
    // one static field and one private native direct method.
    for(uint32_t x:{1,0,1,0,0,9,0,0x102,0}) Leb(b,x);
    U32(b,classes,2); U32(b,classes+4,1); U32(b,classes+8,3);
    U32(b,classes+16,UINT32_MAX); U32(b,classes+24,class_data);
    Finish();
  }
  void Finish() { U32(b,32,b.size()); U32(b,104,b.size()-data); U32(b,108,data); }
};
void Rejected(const Bytes& b) {
  Catalog out; out.types={"stale"}; out.fields.resize(1); out.methods.resize(1);
  std::string error;
  assert(!Read(b.data(),b.size(),&out,&error));
  assert(out.types.empty() && out.fields.empty() && out.methods.empty());
  assert(!error.empty());
}
void Synthetic() {
  Fixture f; Catalog c; std::string error="stale";
  assert(Read(f.b.data(),f.b.size(),&c,&error) && error.empty());
  assert(c.types.size()==6 && c.types[0]=="I" && c.types[1]=="V" && c.types[4]=="[I");
  assert(c.fields[0].declaring_class=="LA;" && c.fields[0].name=="integer" &&
         c.fields[0].descriptor=="I" && c.fields[0].declared && c.fields[0].access==9);
  assert(c.fields[1].declaring_class=="Ljava/lang/Object;" && c.fields[1].descriptor=="[I" &&
         !c.fields[1].declared && !c.fields[1].access);
  assert(c.methods[0].descriptor=="(I)V" && c.methods[0].declared && c.methods[0].access==0x102);
  assert(c.methods[1].declaring_class=="[I" && c.methods[1].descriptor=="()Ljava/lang/Object;" &&
         !c.methods[1].declared);
  // Catalog owns strings, independent of the original buffer lifetime/mutations.
  auto changed=f.b; changed[f.string_data+1]='F';
  assert(c.types[0]=="I");
  for(size_t size=0;size<f.b.size();++size) {
    Bytes truncated(f.b.begin(),f.b.begin()+size); Rejected(truncated);
  }
  for(const auto& edit:std::vector<std::pair<size_t,uint32_t>>{
      {36,120},{40,0x78563412},{108,f.data+1},{104,UINT32_MAX},
      {56,UINT32_MAX},{60,4},{64,65536},{68,f.data},{68,f.strings},
      {f.strings,111},{f.strings,f.b.size()}, {f.types,UINT32_MAX},
      {f.protos,UINT32_MAX},{f.protos+4,999},{f.protos+8,f.list+1},
      {f.list,UINT32_MAX},{f.classes,999},{f.classes+8,0},
      {f.classes+24,f.data-1}}) {
    auto b=f.b; U32(b,edit.first,edit.second); Rejected(b);
  }
  for(const auto& edit:std::vector<std::pair<size_t,uint16_t>>{
      {f.fields,0},{f.fields+2,1},{f.methods+2,99},{f.list+4,1}}) {
    auto b=f.b; U16(b,edit.first,edit.second); Rejected(b);
  }
  auto bad=f.b; bad[6]='1'; bad[5]='4'; Rejected(bad); // container DEX041
  bad=f.b; bad[f.string_data]=2; Rejected(bad); // wrong UTF16 length
  bad=f.b; bad[f.string_data+1]=0xff; Rejected(bad); // invalid MUTF8
  bad=f.b; bad[f.class_data]=0xff; Rejected(bad); // excessive/invalid declarations
  bad=f.b; bad[f.class_data+5]=1; Rejected(bad); // static section mismatch
  bad=f.b; bad[f.class_data+4]=1; Rejected(bad); // external ID declared in wrong owner
  bad=f.b; bad[f.class_data+6]=1; Rejected(bad); // method external owner
  bad=f.b; bad[f.class_data+7]=0x80; bad[f.class_data+8]=2; Rejected(bad); // virtual flags in direct section
  bad=f.b; bad.push_back(0); U32(bad,32,bad.size()); U32(bad,104,bad.size()-f.data);
  bad[f.class_data+6]=0xff; bad[f.class_data+7]=0xff;
  bad[f.class_data+8]=0xff; bad[f.class_data+9]=0xff; bad[f.class_data+10]=0xff;
  Rejected(bad); // fifth-byte ULEB overflow
  bad=f.b; bad[f.class_data+7]=0; bad[f.class_data+8]=0; Rejected(bad); // concrete without code
  assert(!Read(nullptr,0,&c,&error) && c.types.empty());
  assert(!Read(f.b.data(),f.b.size(),nullptr,&error));
  // Every allocation failure refunds partially built catalog, without stale roles.
  unsigned failures=0; bool completed=false;
  for(long nth=0;nth<256;++nth) {
    Catalog out; out.types={"stale"}; out.fields.resize(1); out.methods.resize(1);
    fail_after=nth; const bool ok=Read(f.b.data(),f.b.size(),&out,&error); fail_after=-1;
    if(ok) { completed=true; break; }
    ++failures; assert(out.types.empty() && out.fields.empty() && out.methods.empty());
  }
  assert(completed && failures>=6);
  std::cout<<"Catalog complete external/primitive/array roles, malformed bounds and "
           <<failures<<" allocation refund points PASS\n";
}
void Actual(const char* path) {
  std::ifstream input(path,std::ios::binary); assert(input);
  Bytes b{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  Catalog c; std::string error;
  if(!Read(b.data(),b.size(),&c,&error)) { std::cerr<<error<<'\n'; std::abort(); }
  auto word=[&](size_t off) { return uint32_t(b[off])|uint32_t(b[off+1])<<8|
                                      uint32_t(b[off+2])<<16|uint32_t(b[off+3])<<24; };
  assert(c.types.size()==word(64) && c.fields.size()==word(80) && c.methods.size()==word(88));
  assert(c.types.size()>1024 && c.fields.size()>1024 && c.methods.size()>1024);
  size_t number=SIZE_MAX,field_padding=SIZE_MAX,read=SIZE_MAX,method_padding=SIZE_MAX;
  size_t external_fields=0,external_methods=0;
  for(size_t i=0;i<c.fields.size();++i) {
    const auto& field=c.fields[i]; if(!field.declared)++external_fields;
    if(field.declaring_class!="Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;")continue;
    if(field.name=="number") { assert(field.declared && field.access==0x49 && field.descriptor=="I");number=i; }
    if(field.name=="cachePadding0001")field_padding=i;
  }
  for(size_t i=0;i<c.methods.size();++i) {
    const auto& method=c.methods[i]; if(!method.declared)++external_methods;
    if(method.declaring_class!="Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;")continue;
    if(method.name=="read") { assert(method.declared && method.access==9 && method.descriptor=="()I");read=i; }
    if(method.name=="cachePaddingMethod0001")method_padding=i;
  }
  assert(number!=SIZE_MAX && read!=SIZE_MAX && number==field_padding+1024 && read==method_padding+1024);
  assert(external_methods>0);
  std::cout<<"Actual original cache.dex ALL IDs="<<c.types.size()<<'/'<<c.fields.size()<<'/'
           <<c.methods.size()<<", external refs="<<external_fields<<'/'<<external_methods
           <<", independent declaration flags and pair collisions PASS\n";
}
}  // namespace
int main(int argc,char** argv) { Synthetic(); if(argc>1)Actual(argv[1]); }
