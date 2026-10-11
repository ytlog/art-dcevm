#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::bounded_dex_reader {
struct Dex {
 const unsigned char* bytes;size_t size;
 bool Range(size_t at,size_t n)const{return at<=size&&n<=size-at;}
 bool U16(size_t at,uint16_t* v)const{if(!Range(at,2))return false;*v=uint16_t(bytes[at])|uint16_t(bytes[at+1])<<8;return true;}
 bool U32(size_t at,uint32_t* v)const{if(!Range(at,4))return false;*v=uint32_t(bytes[at])|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;return true;}
 bool Uleb(size_t* at,uint32_t* v)const{uint32_t value=0;for(unsigned i=0;i<5;++i){if(!Range(*at,1))return false;const auto b=bytes[(*at)++];if(i==4&&(b&0xf0))return false;value|=uint32_t(b&127)<<(7*i);if(!(b&128)){*v=value;return true;}}return false;}
};
}
