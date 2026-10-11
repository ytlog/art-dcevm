#include "platform/common/ordinary_source_fixture.h"
namespace artdcevm::ordinary_source_fixture {
namespace {
bool Fail(std::string* error,const char* text){if(error)*error=text;return false;}
bool Member(const evolution::Member& a,const evolution::Member& b){return a.name==b.name&&a.descriptor==b.descriptor&&(a.access&0xffffu)==(b.access&0xffffu);}
bool Same(const evolution::ClassSchema& a,const evolution::ClassSchema& b){
 if(a.descriptor!=b.descriptor||a.superclass!=b.superclass||a.interfaces!=b.interfaces||(a.access&0xffffu)!=(b.access&0xffffu)||a.fields.size()!=b.fields.size()||a.methods.size()!=b.methods.size())return false;
 for(const auto& f:a.fields){size_t matches=0;for(const auto& other:b.fields)matches+=Member(f,other);if(matches!=1)return false;}
 for(const auto& m:a.methods){size_t matches=0;for(const auto& other:b.methods)matches+=Member(m,other);if(matches!=1)return false;}return true;
}
}
bool Validate(const evolution::ClassSchema& live,const std::array<std::vector<unsigned char>,3>& dex,std::string* error){
 std::array<evolution::ClassSchema,3> schemas;
 for(size_t i=0;i<schemas.size();++i)if(!evolution::ReadDexDeclarations(dex[i],&schemas[i],error))return false;
 if(!Same(live,schemas[0]))return Fail(error,"Private ordinary source Class differs from its original immutable DEX");
 for(size_t step=0;step<schemas.size();++step){const auto& s=schemas[step];
  if(s.descriptor!=kDescriptor||s.superclass!="Ljava/lang/Object;"||!s.interfaces.empty()||s.access!=1||s.fields.size()!=2||s.methods.size()!=2+step)
   return Fail(error,"Private ordinary source fixture shape differs from the independent declaration");
  size_t instance=0,statik=0;
  for(const auto& f:s.fields){
   if(f.name=="value"&&f.descriptor=="I"&&f.access==1)++instance;
   else if(f.name=="staticValue"&&f.descriptor=="I"&&f.access==9)++statik;
   else return Fail(error,"Private ordinary source fixture has an unexpected field");
  }
  if(instance!=1||statik!=1)return Fail(error,"Private ordinary source fixture lacks its independent static/instance field witnesses");
  size_t ctor=0,compute=0,first=0,second=0;
  for(const auto& m:s.methods){
   if(m.name=="<init>"&&m.descriptor=="()V"&&m.access==0x10001u)++ctor;
   else if(m.name=="compute"&&m.descriptor=="()I"&&m.access==1)++compute;
   else if(m.name=="first"&&m.descriptor=="()I"&&m.access==1)++first;
   else if(m.name=="second"&&m.descriptor=="()I"&&m.access==1)++second;
   else return Fail(error,"Private ordinary source fixture has an unexpected member");
  }
  if(ctor!=1||compute!=1||first!=(step==2?1u:0u)||second!=(step>0?1u:0u))
   return Fail(error,"Private ordinary source fixture does not perform its two exact additions");
 }
 return true;
}
}
