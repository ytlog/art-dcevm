#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace artdcevm::source_cache_field_witness {
// Fixed libdexfile/dex/modifiers.h kAccJavaFlagsMask (Android 11+).
// ART may add runtime hidden-API flags to ArtField after linking/reflection;
// these do not change Java modifiers or DEX member identity.
inline constexpr uint32_t kJavaFlagsMask=0xffffu;
inline bool Check(void* expected_class,void* declaring_class,const char* name,
                  const char* descriptor,uint32_t native_index,uint32_t dex_index,
                  uint32_t raw_flags,uint32_t public_modifiers,std::string* error){
 auto fail=[&](const char* stage){
  if(error){char text[256];std::snprintf(text,sizeof(text),
    "Public cache Field %s rejected: index=%u expected=%u flags=0x%08x public=0x%08x",
    stage,native_index,dex_index,raw_flags,public_modifiers);*error=text;}
  return false;
 };
 if(!expected_class||declaring_class!=expected_class)return fail("declaring Class identity");
 if(!name||std::strcmp(name,"number"))return fail("DEX name");
 if(!descriptor||std::strcmp(descriptor,"I"))return fail("DEX descriptor");
 if(native_index!=dex_index)return fail("DEX FieldId identity");
 if(public_modifiers!=0x41u)return fail("public modifiers versus immutable DEX");
 if((raw_flags&kJavaFlagsMask)!=public_modifiers)return fail("native Java modifiers versus public oracle");
 return true;
}
}
