#pragma once
#include "platform/common/native_dex_instruction_width.h"
#include <cstring>
#include "platform/common/bounded_dex_reader.h"
namespace artdcevm::source_cache_writer_dex {
// StandardDEX immutable original instructions, not quickened runtime memory.
// Public/native declaration readers separately verify these exact IDs and flags.
inline bool Read(const unsigned char* bytes,size_t size,uint32_t type,uint32_t field,
                 uint32_t padding_field,uint32_t method,uint32_t padding_method,uint32_t writer){
 using namespace bounded_dex_reader;
 if(!bytes||size<112||std::memcmp(bytes,"dex\n",4)||bytes[7])return false;
 Dex d{bytes,size};uint32_t types,fields,methods,fo,mo,classes,co,total;
 if(!d.U32(32,&total)||total!=size||!d.U32(64,&types)||!d.U32(80,&fields)||!d.U32(84,&fo)||!d.U32(88,&methods)||!d.U32(92,&mo)||!d.U32(96,&classes)||!d.U32(100,&co)||!d.Range(fo,size_t(fields)*8)||!d.Range(mo,size_t(methods)*8)||!d.Range(co,size_t(classes)*32))return false;
 if(types<=1024||fields<=1024||methods<=1024||type>=types||field>=fields||padding_field>=fields||method>=methods||padding_method>=methods||writer>=methods||field!=padding_field+1024||method!=padding_method+1024)return false;
 uint16_t owner,caller,a,b;
 if(!d.U16(mo+size_t(writer)*8,&caller)||!d.U16(mo+size_t(method)*8,&owner)||owner!=type||caller==owner||!d.U16(mo+size_t(padding_method)*8,&a)||a!=owner||!d.U16(fo+size_t(field)*8,&a)||a!=owner||!d.U16(fo+size_t(padding_field)*8,&a)||a!=owner)return false;
 bool found=false;size_t cursor=0;
 for(uint32_t i=0;i<classes;++i){uint32_t t,data;if(!d.U32(co+size_t(i)*32,&t)||!d.U32(co+size_t(i)*32+24,&data))return false;if(t==caller){if(found||!data)return false;found=true;cursor=data;}}
 if(!found)return false;uint32_t count[4];for(auto&n:count)if(!d.Uleb(&cursor,&n))return false;
 for(uint64_t i=0;i<uint64_t(count[0])+count[1];++i){uint32_t delta,flags;if(!d.Uleb(&cursor,&delta)||!d.Uleb(&cursor,&flags))return false;}
 uint32_t code=0;
 for(unsigned section=2;section<4;++section){uint32_t idx=0;for(uint32_t i=0;i<count[section];++i){uint32_t delta,flags,c;if(!d.Uleb(&cursor,&delta)||delta>UINT32_MAX-idx||!d.Uleb(&cursor,&flags)||!d.Uleb(&cursor,&c))return false;idx+=delta;if(idx==writer){if(code||section!=2||flags!=8u||!c)return false;code=c;}}}
 uint32_t units;if(!code||!d.U32(code+12,&units)||!d.Range(code+16,size_t(units)*2))return false;
 bool displaced=false,field_seen=false,padding_seen=false,method_seen=false,type_seen=false,new_seen=false;
 for(uint32_t pc=0;pc<units;){uint16_t word;if(!d.U16(code+16+size_t(pc)*2,&word))return false;unsigned op=word&255,width=Width(op);if(!width||width>units-pc||(op==0&&word))return false;
  if(width>=2){if(!d.U16(code+18+size_t(pc)*2,&b))return false;
   if(op==0x62&&b==padding_field)displaced=true;
   if(op==0x60&&b==field){if(!displaced)return false;field_seen=true;}
   if(op==0x1c&&b==type)type_seen=true;
   if(op==0x22&&b==type)new_seen=true;
   if((op==0x71||op==0x77)&&b==padding_method)padding_seen=true;
   if((op==0x71||op==0x77)&&b==method){if(!padding_seen)return false;method_seen=true;}
  }pc+=width;
 }
 return field_seen&&method_seen&&type_seen&&new_seen;
}
}
