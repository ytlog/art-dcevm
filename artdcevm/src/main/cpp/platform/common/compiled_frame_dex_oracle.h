#pragma once
#include "platform/common/native_dex_instruction_width.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace artdcevm::compiled_frame_dex_oracle {
struct Frame {uint16_t registers=0,integer_vreg=0,reference_vreg=0;uint32_t invoke_dex_pc=0;uint16_t outs=0;};
namespace detail {
struct Dex {
 const unsigned char* bytes;size_t size;
 bool Range(size_t at,size_t n)const{return at<=size&&n<=size-at;}
 bool U16(size_t at,uint16_t* v)const{if(!Range(at,2))return false;*v=uint16_t(bytes[at])|uint16_t(bytes[at+1])<<8;return true;}
 bool U32(size_t at,uint32_t* v)const{if(!Range(at,4))return false;*v=uint32_t(bytes[at])|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;return true;}
 bool Uleb(size_t* at,uint32_t* v)const{uint32_t value=0;for(unsigned i=0;i<5;++i){if(!Range(*at,1))return false;const auto b=bytes[(*at)++];if(i==4&&(b&0xf0))return false;value|=uint32_t(b&127)<<(7*i);if(!(b&128)){*v=value;return true;}}return false;}
};
using dex_instruction::Width;

inline bool ChildPc(const Dex& d,size_t code,uint32_t units,uint32_t child,uint32_t* out){
 bool seen=false;uint32_t result=0;
 for(uint32_t pc=0;pc<units;){uint16_t word;if(!d.U16(code+size_t(pc)*2,&word))return false;unsigned op=word&255,w=Width(op);
  if(!w||w>units-pc||(op==0&&word!=0))return false;
  if(op==0x71||op==0x77){uint16_t index;if(!d.U16(code+size_t(pc+1)*2,&index))return false;if(index==child){if(seen)return false;seen=true;result=pc;}}
  pc+=w;
 }
 if(!seen)return false;*out=result;return true;
}
}
// Ordinary installed-APK DEX (035--040, standalone header at zero), independent
// of ArtMethod/CodeInfo. Compact/container 041 inputs are deliberately refused.
// All methods are one declaring class, ordered carrier/middle/inner/native gate.
// Caller separately verifies names/descriptors/modifiers against public methods.
inline bool Read(const unsigned char* bytes,size_t size,const std::array<uint32_t,4>& methods,std::array<Frame,3>* out){
 if(!bytes||!out||size<112||std::memcmp(bytes,"dex\n",4)||bytes[7]||bytes[4]!='0'||bytes[5]<'3'||bytes[5]>'4'||bytes[6]<'0'||bytes[6]>'9')return false;
 const unsigned version=(bytes[5]-'0')*10+bytes[6]-'0';if(version<35||version>40)return false;
 detail::Dex d{bytes,size};uint32_t file_size,header_size,endian,method_count,method_off,class_count,class_off;
 if(!d.U32(32,&file_size)||file_size!=size||!d.U32(36,&header_size)||header_size!=112||!d.U32(40,&endian)||endian!=0x12345678||!d.U32(88,&method_count)||!d.U32(92,&method_off)||!d.Range(method_off,size_t(method_count)*8)||!d.U32(96,&class_count)||!d.U32(100,&class_off)||!d.Range(class_off,size_t(class_count)*32))return false;
 uint16_t owner=0;
 for(size_t i=0;i<4;++i){if(methods[i]>=method_count||methods[i]>UINT16_MAX)return false;for(size_t j=0;j<i;++j)if(methods[i]==methods[j])return false;uint16_t value;if(!d.U16(method_off+size_t(methods[i])*8,&value)||(i&&value!=owner))return false;owner=value;}
 size_t cursor=0;bool found=false;
 for(uint32_t i=0;i<class_count;++i){uint32_t type,data;if(!d.U32(class_off+size_t(i)*32,&type)||!d.U32(class_off+size_t(i)*32+24,&data))return false;if(type==owner){if(found||!data)return false;found=true;cursor=data;}}
 if(!found)return false;
 uint32_t counts[4];for(auto& n:counts)if(!d.Uleb(&cursor,&n))return false;
 const uint64_t total=uint64_t(counts[0])+counts[1]+counts[2]+counts[3];if(total>size)return false;
 for(uint64_t i=0;i<uint64_t(counts[0])+counts[1];++i){uint32_t a,b;if(!d.Uleb(&cursor,&a)||!d.Uleb(&cursor,&b))return false;}
 std::array<Frame,3> result{};unsigned seen=0;
 for(size_t section=2;section<4;++section){uint32_t index=0;
  for(uint32_t i=0;i<counts[section];++i){uint32_t delta,flags,code;if(!d.Uleb(&cursor,&delta)||delta>UINT32_MAX-index||!d.Uleb(&cursor,&flags)||!d.Uleb(&cursor,&code))return false;index+=delta;if(index>=method_count)return false;
   for(size_t frame=0;frame<3;++frame)if(index==methods[frame]){
    if((seen&(1u<<frame))||section!=2||(flags&8)==0||(flags&0x500)!=0||!code||(code&3))return false;
    uint16_t regs,incoming,outs;uint32_t units;if(!d.U16(code,&regs)||!d.U16(code+2,&incoming)||!d.U16(code+4,&outs)||!d.U32(code+12,&units)||!d.Range(code+16,size_t(units)*2)||incoming!=6||regs<incoming||regs>16)return false;
    result[frame].registers=regs;result[frame].outs=outs;result[frame].integer_vreg=regs-incoming+frame*2;result[frame].reference_vreg=result[frame].integer_vreg+1;
    if(!detail::ChildPc(d,code+16,units,methods[frame+1],&result[frame].invoke_dex_pc))return false;seen|=1u<<frame;
   }
  }
 }
 if(seen!=7)return false;*out=result;return true;
}
}
