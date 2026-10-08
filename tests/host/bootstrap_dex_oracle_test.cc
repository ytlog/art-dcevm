#include "platform/common/bootstrap_dex_oracle.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
#include <iostream>
using namespace artdcevm;
int main(int argc,char** argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),{});assert(!bytes.empty());
 evolution::ClassSchema schema;evolution::MappedDexMemberCounts counts;std::string error;
 const char* descriptor="Lcom/github/ytlog/artdcevm/internal/BootstrapAdditive;";
 assert(production_bootstrap::SelectInstalledClass({bytes},descriptor,&schema,&counts,&error));
 assert(schema.descriptor==descriptor&&schema.fields.size()==1&&schema.methods.size()==2&&counts.direct_methods==1&&counts.virtual_methods==1);
 schema.descriptor="preserved";counts.direct_methods=999;
 assert(!production_bootstrap::SelectInstalledClass({bytes,bytes},descriptor,&schema,&counts,&error)&&schema.descriptor=="preserved"&&counts.direct_methods==999);
 assert(!production_bootstrap::SelectInstalledClass({bytes},"Lmissing;",&schema,&counts,&error));
 auto corrupt=bytes;uint32_t overflow=UINT32_MAX;std::memcpy(corrupt.data()+100,&overflow,4);
 assert(!production_bootstrap::SelectInstalledClass({corrupt},descriptor,&schema,&counts,&error));
 bytes.resize(70);assert(!production_bootstrap::SelectInstalledClass({bytes},descriptor,&schema,&counts,&error));
 std::cout<<"Installed APK oracle: complete real DEX declarations, duplicate/missing class and malformed extent refusal preserve output\n";
}
