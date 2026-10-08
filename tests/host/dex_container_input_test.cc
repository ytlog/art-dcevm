#include "evolution/class_schema.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
using namespace artdcevm::evolution;
namespace {
std::vector<unsigned char> File(const char* path) {
  std::ifstream input(path,std::ios::binary);assert(input.good());
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
uint32_t Read(const std::vector<unsigned char>& bytes,size_t offset) {uint32_t value;std::memcpy(&value,bytes.data()+offset,4);return value;}
void Put(std::vector<unsigned char>& bytes,size_t offset,uint32_t value) {std::memcpy(bytes.data()+offset,&value,4);}
void Checksum(std::vector<unsigned char>& bytes) {
  uint32_t a=1,b=0;for(size_t i=12;i<bytes.size();++i){a=(a+bytes[i])%65521;b=(b+a)%65521;}Put(bytes,8,(b<<16)|a);
}
void Same(const ClassSchema& value) {
  assert(value.descriptor=="Ltest/ContainerSubject;" && value.superclass=="Ljava/lang/Object;" && value.access==1);
  assert(value.fields.size()==1 && value.fields[0].name=="value" && value.fields[0].descriptor=="I" && value.fields[0].access==1);
  assert(value.methods.size()==1 && value.methods[0].name=="run" && value.methods[0].descriptor=="()V" && value.methods[0].access==9);
}
void Reject(const std::vector<unsigned char>& bytes) {
  ClassSchema value;value.descriptor="Lsentinel;";std::string error;
  assert(!ReadDexDeclarations(bytes,&value,&error));assert(value.descriptor=="Lsentinel;" && !error.empty());
  assert(!ReadDexSchema(bytes,&value,&error));assert(value.descriptor=="Lsentinel;" && !error.empty());
}
}
int main(int argc,char** argv) {
  assert(argc==3);const auto old=File(argv[1]),original=File(argv[2]);std::string error;
  for(const auto* bytes:{&old,&original}) {
    MappedDexMemberCounts counts;
    assert(ReadMappedDexMemberCounts({bytes->data(),bytes->size(),bytes->data(),bytes->size(),0},&counts,&error));
    assert(counts.static_fields==0 && counts.instance_fields==1 && counts.direct_methods==1 && counts.virtual_methods==0);
    ClassSchema value;assert(ReadDexDeclarations(*bytes,&value,&error));Same(value);
    assert(ReadDexSchema(*bytes,&value,&error));Same(value);
    assert(ReadMappedDexDeclarations({bytes->data(),bytes->size(),bytes->data(),bytes->size(),0},&value,&error));Same(value);
  }
  for(auto change:{std::pair<size_t,uint32_t>{32,0x78},{36,0x70},{104,1},{108,0x78},
      {0x70,0},{0x70,UINT32_MAX},{0x74,4},{60,UINT32_MAX},{64,UINT32_MAX},{68,Read(original,60)},
      {96,2},{100,Read(original,60)}}) {
    auto bytes=original;Put(bytes,change.first,change.second);Checksum(bytes);Reject(bytes);
  }
  auto bytes=original;bytes[8]^=1;Reject(bytes);
  // Raw replacements cannot silently select one file in a multi-header container.
  bytes=original;bytes.insert(bytes.end(),original.begin(),original.end());Put(bytes,0x70,bytes.size());Checksum(bytes);Reject(bytes);
  // Variable metadata and concrete code must not borrow bytes from ID tables.
  bytes=original;Put(bytes,Read(original,60),Read(original,68));Checksum(bytes);Reject(bytes);
  bytes=original;
  const uint32_t class_data=Read(original,Read(original,100)+24);
  bytes[class_data+8]=0x80;bytes[class_data+9]=0x01;Checksum(bytes);Reject(bytes);
  bytes=original;bytes[class_data+8]=0xff;bytes[class_data+9]=0x7f;Checksum(bytes);Reject(bytes);
  for(size_t length=0;length<original.size();++length) {
    bytes=original;bytes.resize(length);Reject(bytes);
  }
  std::mt19937 random(0xde0041);
  for(unsigned i=0;i<10000;++i) {
    bytes=original;
    for(unsigned n=0;n<1+random()%8;++n)bytes[random()%bytes.size()]^=1+random()%255;
    Checksum(bytes);
    ClassSchema value;value.descriptor="Lsentinel;";
    if(!ReadDexDeclarations(bytes,&value,&error))assert(value.descriptor=="Lsentinel;" && !error.empty());
  }
  std::cout<<"DEX 040/041 replacement input: class/field/concrete method metadata, checksums, container/ID/code bounds, single logical DEX, truncation and 10000 mutations passed\n";
}
