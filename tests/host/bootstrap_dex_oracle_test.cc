#include "platform/common/bootstrap_dex_oracle.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
#include <iostream>
using namespace artdcevm;
int main(int argc,char** argv){
 assert(argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});assert(!bytes.empty());
 production_bootstrap::InstalledClassIds ids;evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;std::string error;
 const char* descriptor="Lcom/github/ytlog/artdcevm/internal/BootstrapAdditive;";
 assert(production_bootstrap::SelectInstalledClass({bytes},descriptor,&schema,&counts,&error,&ids));
 assert(schema.descriptor==descriptor&&schema.fields.size()==1&&schema.methods.size()==2&&counts.direct_methods==1&&counts.virtual_methods==1);
 auto read=[](const unsigned char*p){uint32_t v;std::memcpy(&v,p,4);return v;};
 assert(ids.type_count==read(bytes.data()+64)&&ids.field_count==read(bytes.data()+80)&&ids.method_count==read(bytes.data()+88)&&ids.type_index<ids.type_count);
 for(const auto&field:schema.fields)assert(field.dex_index<ids.field_count);
 for(const auto&method:schema.methods)assert(method.dex_index<ids.method_count);
 schema.descriptor="preserved";counts.direct_methods=999;ids.type_index=999;
 assert(!production_bootstrap::SelectInstalledClass({bytes,bytes},descriptor,&schema,&counts,&error,&ids)&&schema.descriptor=="preserved"&&counts.direct_methods==999&&ids.type_index==999);
 assert(!production_bootstrap::SelectInstalledClass({bytes},"Lmissing;",&schema,&counts,&error));
 auto corrupt=bytes;uint32_t overflow=UINT32_MAX;std::memcpy(corrupt.data()+100,&overflow,4);
 assert(!production_bootstrap::SelectInstalledClass({corrupt},descriptor,&schema,&counts,&error));
 bytes.resize(70);assert(!production_bootstrap::SelectInstalledClass({bytes},descriptor,&schema,&counts,&error));
 std::ifstream cache_file(argv[2],std::ios::binary);std::vector<unsigned char> cache_bytes((std::istreambuf_iterator<char>(cache_file)),{});
 assert(!cache_bytes.empty());production_bootstrap::InstalledClassIds cache_ids;evolution::ClassSchema cache_schema;
 assert(production_bootstrap::SelectInstalledClass({cache_bytes},"Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;",&cache_schema,&counts,&error,&cache_ids));
 assert(cache_ids.type_count>1024&&cache_ids.field_count>1024&&cache_ids.method_count>1024);
 bool number=false;for(const auto& field:cache_schema.fields)number|=field.name=="number"&&field.descriptor=="I"&&field.access==0x41u;assert(number);
 assert(cache_schema.fields.size()==1026&&cache_schema.methods.size()==1028&&cache_ids.type_index<cache_ids.type_count);
 bool found_read=false,found_warm=false;
 for(const auto& method:cache_schema.methods){
  assert(method.dex_index<cache_ids.method_count);
  found_read|=method.name=="read"&&method.descriptor=="()I"&&method.access==1u;
  found_warm|=method.name=="warm"&&method.descriptor=="(Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;)Ljava/lang/Class;"&&method.access==9u;
 }
 assert(found_read&&found_warm);
 std::cout<<"Installed APK oracle: complete real DEX declarations, duplicate/missing class and malformed extent refusal preserve output\n";
}
