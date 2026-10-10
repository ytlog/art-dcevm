#pragma once
#include "evolution/class_schema.h"
#include <cstring>
#include <string_view>
namespace artdcevm::receiver_guard {
// Allocation-free counterpart of the shared mapped DEX identity/direct-list
// reader. Fixed DexFile Header/ClassDef/TypeList/StringDataItem declarations;
// malformed input returns false without constructing an exception/diagnostic.
class BoundedDexView final {
 public:
 explicit BoundedDexView(const evolution::MappedDexMetadata& m):header_(m.begin),base_(m.begin),data_(m.data_begin),size_(m.size),data_size_(m.data_size),index_(m.class_def_index){}
 bool Initialize()noexcept {
  if(!header_||!data_||!Range(size_,0,112))return false;
  const bool compact=!std::memcmp(header_,"cdex001\0",8),container=!compact&&!std::memcmp(header_,"dex\n041\0",8);
  header_size_=compact?136:container?120:112;
  if(!Range(size_,0,header_size_)||Word(header_+36)!=header_size_||Word(header_+40)!=0x12345678)return false;
  if(!compact&&(std::memcmp(header_,"dex\n",4)!=0||header_[7]||
     (std::memcmp(header_+4,"035",3)&&std::memcmp(header_+4,"037",3)&&std::memcmp(header_+4,"038",3)&&std::memcmp(header_+4,"039",3)&&std::memcmp(header_+4,"040",3)&&!container)))return false;
  if(container){const size_t total=Word(header_+112),offset=Word(header_+116),file=Word(header_+32);
   if(total>data_size_||offset>total||file<header_size_||file>size_||file>total-offset||uintptr_t(header_)<uintptr_t(data_)||uintptr_t(header_)-uintptr_t(data_)!=offset||Word(header_+104)||Word(header_+108))return false;
   base_=data_;size_=data_size_=total;
  }else if(Word(header_+108)>data_size_)return false;
  if(!Table(56,4,&strings_)||!Table(64,4,&types_)||!Table(96,32,&classes_)||index_>=classes_.count)return false;
  definition_=classes_.offset+size_t(index_)*32;initialized_=true;return true;
 }
 bool Identity(const char** descriptor,uint32_t* flags,uint32_t* type)const noexcept {
  if(!initialized_||!descriptor||!flags||!type)return false;
  const auto id=Word(base_+definition_);std::string_view text;
  if(!Type(id,&text)||text.empty()||text[0]!='L')return false;
  *descriptor=text.data();*flags=Word(base_+definition_+4);*type=id;return true;
 }
 bool DirectCount(uint32_t*out)const noexcept {
  if(!initialized_||!out)return false;const auto off=Word(base_+definition_+12);
  if(!off){*out=0;return true;}if(off%4||!Range(data_size_,off,4))return false;
  const auto count=Word(data_+off);if(!Range(data_size_,size_t(off)+4,size_t(count)*2))return false;
  for(uint32_t i=0;i<count;++i){std::string_view a;if(!DirectUnchecked(off,i,&a)||a.empty()||a[0]!='L')return false;
   for(uint32_t j=0;j<i;++j){std::string_view b;if(!DirectUnchecked(off,j,&b)||a==b)return false;}
  }*out=count;return true;
 }
 bool DirectAt(uint32_t i,const char**out)const noexcept {
  uint32_t count=0;if(!out||!DirectCount(&count)||i>=count)return false;
  std::string_view descriptor;if(!DirectUnchecked(Word(base_+definition_+12),i,&descriptor))return false;
  *out=descriptor.data();return true;
 }
 private:
 struct Ids {size_t offset=0;uint32_t count=0;};
 static bool Range(size_t limit,size_t off,size_t bytes)noexcept{return off<=limit&&bytes<=limit-off;}
 static uint32_t Word(const unsigned char*p)noexcept{uint32_t n;std::memcpy(&n,p,4);return n;}
 bool Table(size_t header,uint32_t width,Ids*out)const noexcept {const auto n=Word(header_+header),off=Word(header_+header+4);
  if((n==0)!=(off==0)||(n&&(off<header_size_||off%4))||!Range(size_,off,size_t(n)*width))return false;*out={off,n};return true;}
 bool Leb(size_t*cursor,uint32_t*out)const noexcept {uint32_t value=0;for(unsigned i=0;i<5;++i){if(!Range(data_size_,*cursor,1))return false;const auto b=data_[(*cursor)++];if(i==4&&(b&0xf0))return false;value|=uint32_t(b&0x7f)<<(7*i);if(!(b&0x80)){*out=value;return true;}}return false;}
 bool String(uint32_t i,std::string_view*out)const noexcept {
  if(i>=strings_.count)return false;size_t cursor=Word(base_+strings_.offset+size_t(i)*4);uint32_t units=0,read=0;
  if(!Leb(&cursor,&units))return false;const size_t start=cursor;
  for(;;){if(!Range(data_size_,cursor,1))return false;const auto b=data_[cursor++];if(!b)break;if(read==UINT32_MAX)return false;++read;if(b<0x80)continue;
   const unsigned more=(b&0xe0)==0xc0?1:(b&0xf0)==0xe0?2:0;if(!more)return false;uint32_t scalar=b&(more==1?0x1f:0x0f);
   for(unsigned j=0;j<more;++j){if(!Range(data_size_,cursor,1))return false;const auto next=data_[cursor++];if((next&0xc0)!=0x80)return false;scalar=(scalar<<6)|(next&0x3f);}
   if((more==1&&scalar&&scalar<0x80)||(more==2&&scalar<0x800))return false;
  }if(units!=read)return false;*out={reinterpret_cast<const char*>(data_+start),cursor-start-1};return true;
 }
 bool Type(uint32_t i,std::string_view*out)const noexcept {
  if(i>=types_.count||!String(Word(base_+types_.offset+size_t(i)*4),out)||out->empty())return false;
  size_t at=0,arrays=0;while(at<out->size()&&(*out)[at]=='['){++at;if(++arrays>255)return false;}if(at==out->size())return false;
  const auto ch=(*out)[at++];if(ch=='V')return !arrays&&at==out->size();if(std::strchr("ZBSCIJFD",ch))return at==out->size();if(ch!='L'||at+1>=out->size()||out->back()!=';')return false;
  for(;at+1<out->size();++at)if((*out)[at]=='.'||(*out)[at]=='['||(*out)[at]=='('||(*out)[at]==')'||(*out)[at]==';'||!(*out)[at])return false;return true;
 }
 bool DirectUnchecked(uint32_t offset,uint32_t i,std::string_view*out)const noexcept {const auto p=data_+size_t(offset)+4+size_t(i)*2;uint16_t index;std::memcpy(&index,p,2);return Type(index,out);}
 const unsigned char*header_,*base_,*data_;size_t size_,data_size_,header_size_=0,definition_=0;uint32_t index_;Ids strings_,types_,classes_;bool initialized_=false;
};
}
