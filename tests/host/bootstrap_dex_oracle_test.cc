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
 uint32_t class_index=123;
 assert(evolution::FindDexClassDefinition(bytes,descriptor,&class_index,&error)&&class_index!=UINT32_MAX);
 evolution::ClassSchema target;
 assert(evolution::ReadDexDeclarations(bytes,descriptor,&target,&error)&&
        target.descriptor==schema.descriptor&&target.fields.size()==schema.fields.size()&&target.methods.size()==schema.methods.size());
 assert(evolution::FindDexClassDefinition(bytes,"Lmissing;",&class_index,&error)&&class_index==UINT32_MAX);
 target.descriptor="preserved";
 assert(!evolution::ReadDexDeclarations(bytes,"Lmissing;",&target,&error)&&target.descriptor=="preserved");
 auto read=[](const unsigned char*p){uint32_t v;std::memcpy(&v,p,4);return v;};
 assert(ids.type_count==read(bytes.data()+64)&&ids.field_count==read(bytes.data()+80)&&ids.method_count==read(bytes.data()+88)&&ids.type_index<ids.type_count);
 for(const auto&field:schema.fields)assert(field.dex_index<ids.field_count);
 for(const auto&method:schema.methods)assert(method.dex_index<ids.method_count);
 schema.descriptor="preserved";counts.direct_methods=999;ids.type_index=999;
 assert(!production_bootstrap::SelectInstalledClass({bytes,bytes},descriptor,&schema,&counts,&error,&ids)&&schema.descriptor=="preserved"&&counts.direct_methods==999&&ids.type_index==999);
 assert(!production_bootstrap::SelectInstalledClass({bytes},"Lmissing;",&schema,&counts,&error));
 auto corrupt=bytes;uint32_t overflow=UINT32_MAX;std::memcpy(corrupt.data()+100,&overflow,4);
 assert(!production_bootstrap::SelectInstalledClass({corrupt},descriptor,&schema,&counts,&error));
 class_index=123;assert(!evolution::FindDexClassDefinition(corrupt,descriptor,&class_index,&error)&&class_index==123);
 assert(!evolution::ReadDexDeclarations(corrupt,descriptor,&target,&error)&&target.descriptor=="preserved");
 bytes.resize(70);assert(!production_bootstrap::SelectInstalledClass({bytes},descriptor,&schema,&counts,&error));
 std::ifstream cache_file(argv[2],std::ios::binary);std::vector<unsigned char> cache_bytes((std::istreambuf_iterator<char>(cache_file)),{});
 assert(!cache_bytes.empty());production_bootstrap::InstalledClassIds cache_ids;evolution::ClassSchema cache_schema;
 assert(production_bootstrap::SelectInstalledClass({cache_bytes},"Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;",&cache_schema,&counts,&error,&cache_ids));
 assert(cache_ids.type_count>1024&&cache_ids.field_count>1024&&cache_ids.method_count>1024);
 bool number=false;for(const auto& field:cache_schema.fields)number|=field.name=="number"&&field.descriptor=="I"&&field.access==0x49u;assert(number);
 assert(cache_schema.fields.size()==1026&&cache_schema.methods.size()==1029&&cache_ids.type_index<cache_ids.type_count);
 assert(counts.static_fields==1026&&counts.instance_fields==0&&counts.direct_methods==1029&&counts.virtual_methods==0);
 bool found_read=false,found_warm=false,found_clinit=false;
 for(const auto& method:cache_schema.methods){
  assert(method.dex_index<cache_ids.method_count);
  found_clinit|=method.name=="<clinit>"&&method.descriptor=="()V"&&method.access==0x10008u;
  found_read|=method.name=="read"&&method.descriptor=="()I"&&method.access==9u;
  found_warm|=method.name=="warm"&&method.descriptor=="(Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;)Ljava/lang/Class;"&&method.access==9u;
 }
 assert(found_read&&found_warm&&found_clinit);
 evolution::ClassSchema writer;production_bootstrap::InstalledClassIds writer_ids;
 assert(production_bootstrap::SelectInstalledClass({cache_bytes},"Lcom/github/ytlog/artdcevm/internal/ZCacheWriter;",&writer,&counts,&error,&writer_ids));
 assert(writer_ids.type_index!=cache_ids.type_index&&writer_ids.type_count==cache_ids.type_count&&writer_ids.field_count==cache_ids.field_count&&writer_ids.method_count==cache_ids.method_count);
 assert(evolution::FindDexClassDefinition(cache_bytes,writer.descriptor,&class_index,&error)&&class_index!=UINT32_MAX);
 assert(evolution::ReadDexClassSchema(cache_bytes,class_index,&target,&error)&&target.descriptor==writer.descriptor&&target.methods.size()==2);
 for(const auto& method:target.methods)assert((method.access&0x10000u)==0);
 assert(evolution::ReadDexDeclarations(cache_bytes,writer.descriptor,&target,&error)&&
        target.descriptor==writer.descriptor&&target.fields.empty()&&target.methods.size()==2);
 assert(evolution::ReadDexDeclarations(cache_bytes,cache_schema.descriptor,&target,&error)&&
        target.descriptor==cache_schema.descriptor&&target.fields.size()==1026&&target.methods.size()==1029);
 assert(writer.fields.empty()&&writer.methods.size()==2&&counts.direct_methods==2&&counts.virtual_methods==0);
 // Duplicate ClassDefs within one valid-checksum file must not silently
 // choose whichever occurrence appears first.
 assert(read(cache_bytes.data()+96)>1&&class_index!=UINT32_MAX);
 auto duplicate=cache_bytes;const size_t class_table=read(duplicate.data()+100);
 const uint32_t other=class_index?0:1;
 std::memcpy(duplicate.data()+class_table+size_t(other)*32,duplicate.data()+class_table+size_t(class_index)*32,32);
 uint32_t checksum_a=1,checksum_b=0;
 for(size_t n=12;n<duplicate.size();++n){checksum_a=(checksum_a+duplicate[n])%65521;checksum_b=(checksum_b+checksum_a)%65521;}
 const uint32_t checksum=(checksum_b<<16)|checksum_a;std::memcpy(duplicate.data()+8,&checksum,4);
 class_index=123;target.descriptor="preserved";
 assert(!evolution::FindDexClassDefinition(duplicate,writer.descriptor,&class_index,&error)&&class_index==123);
 assert(!evolution::ReadDexDeclarations(duplicate,writer.descriptor,&target,&error)&&target.descriptor=="preserved");
 assert(!production_bootstrap::SelectInstalledClass({duplicate},writer.descriptor.c_str(),&target,&counts,&error)&&target.descriptor=="preserved");
 bool cross_warm=false;for(const auto&method:writer.methods)cross_warm|=method.name=="warm"&&method.descriptor=="(Lcom/github/ytlog/artdcevm/internal/SourceCacheFixture;)Ljava/lang/Class;"&&method.access==8u;
 assert(cross_warm);
 std::cout<<"Installed APK oracle: complete real DEX declarations, duplicate/missing class and malformed extent refusal preserve output\n";
}
