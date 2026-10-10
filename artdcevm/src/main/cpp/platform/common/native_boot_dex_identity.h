#pragma once
#include "evolution/class_schema.h"
#include <cstring>
#include <string_view>
namespace artdcevm::native_boot_aot_unaffected {
// Borrowed ID-section reader for the immutable standard/compact/container DEX
// format. Every failure is a value: stopped verification must never construct
// parser exceptions or allocate descriptor strings.
class DexIdentity {
 public:
 explicit DexIdentity(const evolution::MappedDexMetadata& map):m_(map){}
 bool ReturnsVoid(uint32_t index)noexcept{
  if(!Initialize())return false;
  size_t method=0,proto=0;uint16_t prototype=0;uint32_t ret=0;std::string_view value;
  return Item(88,8,index,&method)&&Read(ids_,id_size_,method+2,&prototype)&&
    Item(72,12,prototype,&proto)&&Read(ids_,id_size_,proto+4,&ret)&&Type(ret,&value)&&value=="V";
 }
 bool Matches(uint32_t index,std::string_view owner,std::string_view name,std::string_view signature)noexcept{
  if(!Initialize())return false;
  size_t method=0,proto=0;uint16_t type=0,prototype=0;uint32_t string=0,ret=0,parameters=0;
  std::string_view value;
  if(!Item(88,8,index,&method)||!Read(ids_,id_size_,method,&type)||!Read(ids_,id_size_,method+2,&prototype)||
     !Read(ids_,id_size_,method+4,&string)||!Type(type,&value)||value!=owner||!String(string,&value)||value!=name||
     !Item(72,12,prototype,&proto)||!Read(ids_,id_size_,proto+4,&ret)||!Read(ids_,id_size_,proto+8,&parameters)||
     signature.empty()||signature.front()!='(')return false;
  signature.remove_prefix(1);
  auto consume=[&](std::string_view part){if(signature.substr(0,part.size())!=part)return false;signature.remove_prefix(part.size());return true;};
  if(parameters){uint32_t count=0;if(parameters%4||!Read(m_.data_begin,m_.data_size,parameters,&count)||uint64_t(count)*2>m_.data_size||parameters>m_.data_size-4||uint64_t(count)*2>m_.data_size-parameters-4)return false;
   for(uint32_t i=0;i<count;++i){uint16_t parameter=0;if(!Read(m_.data_begin,m_.data_size,size_t(parameters)+4+size_t(i)*2,&parameter)||!Type(parameter,&value)||value=="V"||!consume(value))return false;}}
  if(signature.empty()||signature.front()!=')')return false;signature.remove_prefix(1);
  return Type(ret,&value)&&consume(value)&&signature.empty();
 }
 private:
 template<class T>static bool Read(const unsigned char*p,size_t bytes,size_t offset,T*out)noexcept{if(!p||offset>bytes||sizeof(T)>bytes-offset)return false;std::memcpy(out,p+offset,sizeof(T));return true;}
 bool Initialize()noexcept{
  if(!m_.begin||!m_.data_begin||m_.size<112)return false;
  const bool compact=!std::memcmp(m_.begin,"cdex001\0",8),container=!std::memcmp(m_.begin,"dex\n041\0",8);
  if(!compact&&(std::memcmp(m_.begin,"dex\n",4)||m_.begin[7]))return false;
  uint32_t size=0,endian=0;if(!Read(m_.begin,m_.size,36,&size)||!Read(m_.begin,m_.size,40,&endian)||size!=(compact?136u:container?120u:112u)||size>m_.size||endian!=0x12345678)return false;
  ids_=m_.begin;id_size_=m_.size;
  if(container){uint32_t total=0,offset=0;if(!Read(m_.begin,m_.size,112,&total)||!Read(m_.begin,m_.size,116,&offset)||total>m_.data_size||offset>total||uintptr_t(m_.begin)<uintptr_t(m_.data_begin)||uintptr_t(m_.begin)-uintptr_t(m_.data_begin)!=offset)return false;ids_=m_.data_begin;id_size_=total;}
  return true;
 }
 bool Item(size_t header,size_t width,uint32_t index,size_t*out)const noexcept{
  uint32_t count=0,offset=0;if(!Read(m_.begin,m_.size,header,&count)||!Read(m_.begin,m_.size,header+4,&offset)||index>=count||offset%4||offset>id_size_||uint64_t(count)*width>id_size_-offset)return false;
  *out=size_t(offset)+size_t(index)*width;return true;
 }
 bool String(uint32_t index,std::string_view*out)const noexcept{
  size_t item=0;uint32_t offset=0;if(!Item(56,4,index,&item)||!Read(ids_,id_size_,item,&offset)||offset>=m_.data_size)return false;
  size_t cursor=offset;bool terminated=false;for(unsigned i=0;i<5&&cursor<m_.data_size;++i){const auto byte=m_.data_begin[cursor++];if(i==4&&(byte&0xf0))return false;if(!(byte&0x80)){terminated=true;break;}}
  if(!terminated)return false;const auto*start=m_.data_begin+cursor;const auto*end=static_cast<const unsigned char*>(std::memchr(start,0,m_.data_size-cursor));if(!end)return false;
  *out={reinterpret_cast<const char*>(start),size_t(end-start)};return true;
 }
 bool Type(uint32_t index,std::string_view*out)const noexcept{size_t item=0;uint32_t string=0;return Item(64,4,index,&item)&&Read(ids_,id_size_,item,&string)&&String(string,out)&&!out->empty();}
 const evolution::MappedDexMetadata&m_;const unsigned char*ids_=nullptr;size_t id_size_=0;
};
}
