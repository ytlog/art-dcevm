#include "platform/common/ordinary_source_fixture.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
using namespace artdcevm;
int main(int argc,char** argv){
 assert(argc==4);std::array<std::vector<unsigned char>,3> dex;
 for(size_t i=0;i<dex.size();++i){std::ifstream in(argv[i+1],std::ios::binary);assert(in);dex[i]={std::istreambuf_iterator<char>(in),{}};}
 evolution::ClassSchema live;std::string error;assert(evolution::ReadDexDeclarations(dex[0],&live,&error));
 // JVMTI public method modifiers do not contain the DEX constructor bit.
 for(auto& member:live.methods)member.access&=0xffffu;
 assert(ordinary_source_fixture::Validate(live,dex,&error));
 auto bad=live;bad.superclass="LForeign;";assert(!ordinary_source_fixture::Validate(bad,dex,&error));
 bad=live;bad.fields[0].access=2;assert(!ordinary_source_fixture::Validate(bad,dex,&error));
 bad=live;bad.methods[0].name="foreign";assert(!ordinary_source_fixture::Validate(bad,dex,&error));
 // The former deletion-first sequence would require an already authorized
 // private commit engine. It must not be accepted as bootstrap calibration.
 auto reversed=dex;std::swap(reversed[0],reversed[2]);
 evolution::ClassSchema full;assert(evolution::ReadDexDeclarations(reversed[0],&full,&error));
 for(auto& member:full.methods)member.access&=0xffffu;
 assert(!ordinary_source_fixture::Validate(full,reversed,&error));
 auto corrupt=dex;corrupt[1]=dex[0];assert(!ordinary_source_fixture::Validate(live,corrupt,&error));
 corrupt=dex;corrupt[2]=dex[1];assert(!ordinary_source_fixture::Validate(live,corrupt,&error));
 corrupt=dex;corrupt[0].resize(20);assert(!ordinary_source_fixture::Validate(live,corrupt,&error));
 std::cout<<"Actual generated private DEX: public original/complete immutable three-stage declarations, two exact additions, foreign/altered/duplicate/corrupt definitions refuse PASS\n";
}
