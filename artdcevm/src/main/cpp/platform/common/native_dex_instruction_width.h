#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::dex_instruction {
// DEX instruction code-unit widths. Zero means an unsupported/reserved opcode.
// Payload extents are data-dependent and must be checked by the stream reader.
inline unsigned Width(unsigned op){
 if(op==3||op==6||op==9||op==0x14||op==0x17||op==0x1b||op==0x24||op==0x25||op==0x26||op==0x2a||op==0x2b||op==0x2c||(op>=0x6e&&op<=0x72)||(op>=0x74&&op<=0x78)||op==0xfc||op==0xfd)return 3;
 if(op==0x18)return 5;if(op==0xfa||op==0xfb)return 4;
 if(op==2||op==5||op==8||op==0x13||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x1c||op==0x1f||op==0x20||op==0x22||op==0x23||op==0x29||(op>=0x2d&&op<=0x3d)||(op>=0x44&&op<=0x6d)||(op>=0x90&&op<=0xaf)||(op>=0xd0&&op<=0xe2)||op==0xfe||op==0xff)return 2;
 if(op<=0x12||op==0x1d||op==0x1e||op==0x21||op==0x27||op==0x28||(op>=0x7b&&op<=0x8f)||(op>=0xb0&&op<=0xcf))return 1;
 return 0;
}
// One bounded instruction or payload, without interpreting operand words.
inline bool Step(const uint16_t*code,size_t units,size_t pc,uint32_t*out,bool*payload)noexcept{
 if(!code||!out||!payload||pc>=units)return false;
 const uint16_t word=code[pc];uint64_t width=Width(word&255u);
 const bool data=(word&255u)==0&&word!=0;
 if(data){
  if((pc&1)||units-pc<2)return false;
  const unsigned kind=word>>8;
  if(kind==1)width=4ull+uint64_t(code[pc+1])*2;
  else if(kind==2)width=2ull+uint64_t(code[pc+1])*4;
  else if(kind==3){
   if(units-pc<4)return false;const unsigned element=code[pc+1];
   if(element!=1&&element!=2&&element!=4&&element!=8)return false;
   const uint32_t count=uint32_t(code[pc+2])|(uint32_t(code[pc+3])<<16);
   width=4+(uint64_t(element)*count+1)/2;
  }else return false;
 }
 if(!width||width>UINT32_MAX||width>units-pc)return false;
 *out=uint32_t(width);*payload=data;return true;
}
}
