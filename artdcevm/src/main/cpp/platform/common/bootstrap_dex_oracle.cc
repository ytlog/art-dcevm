#include "platform/common/bootstrap_dex_oracle.h"
#include <cstring>
namespace artdcevm::production_bootstrap {
namespace {uint32_t U32(const unsigned char* p){uint32_t v;std::memcpy(&v,p,4);return v;}}
bool SelectInstalledClass(const std::vector<std::vector<unsigned char>>& files,const char* wanted,
    evolution::ClassSchema* output,evolution::MappedDexMemberCounts* counts,std::string* error,InstalledClassIds* ids,
    InstalledClassLocation* location){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!wanted||!output||!counts||!error)return fail("Invalid installed APK oracle output");
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts selected;InstalledClassIds selected_ids;
 InstalledClassLocation selected_location;bool found=false;
 for(size_t file=0;file<files.size();++file){const auto&bytes=files[file];
  if(bytes.size()<112||std::memcmp(bytes.data(),"dex\n",4)||bytes[7]||U32(bytes.data()+32)!=bytes.size())return fail("Installed APK DEX header is not a complete standard file");
  uint32_t k=UINT32_MAX;
  if(!evolution::FindDexClassDefinition(bytes,wanted,&k,error))return false;
  if(k==UINT32_MAX)continue;
  if(found)return fail("Ambiguous calibration class in installed APK DEX");
  const uint32_t at=U32(bytes.data()+100);
  evolution::MappedDexMetadata view{bytes.data(),bytes.size(),bytes.data(),bytes.size(),k};
  if(!evolution::ReadMappedDexDeclarations(view,&schema,error)||
     !evolution::ReadMappedDexMemberCounts(view,&selected,error))return false;
  selected_ids={U32(bytes.data()+at+size_t(k)*32),U32(bytes.data()+64),U32(bytes.data()+80),U32(bytes.data()+88)};
  selected_location={file,k};
  if(selected_ids.type_index>=selected_ids.type_count)return fail("Installed APK class type index exceeds independent type table");
  found=true;
 }
 if(!found)return fail("Calibration class absent from original installed APK DEX");
 *output=std::move(schema);*counts=selected;if(ids)*ids=selected_ids;if(location)*location=selected_location;error->clear();return true;
}
}
