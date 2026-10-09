#include "platform/common/bootstrap_dex_oracle.h"
#include <cstring>
namespace artdcevm::production_bootstrap {
namespace {uint32_t U32(const unsigned char* p){uint32_t v;std::memcpy(&v,p,4);return v;}}
bool SelectInstalledClass(const std::vector<std::vector<unsigned char>>& files,const char* wanted,
    evolution::ClassSchema* output,evolution::MappedDexMemberCounts* counts,std::string* error,InstalledClassIds* ids){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!wanted||!output||!counts||!error)return fail("Invalid installed APK oracle output");
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts selected;InstalledClassIds selected_ids;bool found=false;
 for(const auto& bytes:files){
  if(bytes.size()<112||std::memcmp(bytes.data(),"dex\n",4)||bytes[7]||U32(bytes.data()+32)!=bytes.size())return fail("Installed APK DEX header is not a complete standard file");
  const uint32_t size=U32(bytes.data()+104),offset=U32(bytes.data()+108),classes=U32(bytes.data()+96),at=U32(bytes.data()+100);
  if(offset>bytes.size()||size>bytes.size()-offset||at>bytes.size()||uint64_t(classes)*32>bytes.size()-at)return fail("Installed APK class/data extent exceeds original bytes");
  for(uint32_t k=0;k<classes;++k){
   // StandardDex offsets are relative to the original file beginning.
   evolution::MappedDexMetadata view{bytes.data(),bytes.size(),bytes.data(),bytes.size(),k};const char* descriptor=nullptr;
   if(!evolution::ReadMappedDexTypeDescriptor(view,U32(bytes.data()+at+k*32),&descriptor,error)||!descriptor)return false;
   if(std::strcmp(descriptor,wanted))continue;
   if(found)return fail("Ambiguous calibration class in installed APK DEX");
   if(!evolution::ReadMappedDexDeclarations(view,&schema,error)||!evolution::ReadMappedDexMemberCounts(view,&selected,error))return false;
   selected_ids={U32(bytes.data()+at+k*32),U32(bytes.data()+64),U32(bytes.data()+80),U32(bytes.data()+88)};
   if(selected_ids.type_index>=selected_ids.type_count)return fail("Installed APK class type index exceeds independent type table");
   found=true;
  }
 }
 if(!found)return fail("Calibration class absent from original installed APK DEX");
 *output=std::move(schema);*counts=selected;if(ids)*ids=selected_ids;error->clear();return true;
}
}
