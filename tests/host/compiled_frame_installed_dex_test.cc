#include "platform/common/bootstrap_dex_oracle.h"
#include "platform/common/compiled_frame_dex_oracle.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc,char** argv){assert(argc==2);std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),{}};assert(!bytes.empty());artdcevm::evolution::ClassSchema schema;artdcevm::evolution::MappedDexMemberCounts counts;std::string error;assert(artdcevm::production_bootstrap::SelectInstalledClass({bytes},"Lcom/github/ytlog/artdcevm/internal/CompiledFrameFixture;",&schema,&counts,&error));std::array<uint32_t,4> indices{};const char* names[]={"carrier","middle","inner","awaitGate"};for(unsigned i=0;i<4;++i){bool found=false;for(const auto& m:schema.methods)if(m.name==names[i]){assert(!found);found=true;indices[i]=m.dex_index;}assert(found);}std::array<artdcevm::compiled_frame_dex_oracle::Frame,3> frames;assert(artdcevm::compiled_frame_dex_oracle::Read(bytes.data(),bytes.size(),indices,&frames));for(unsigned i=0;i<3;++i)std::printf("actual D8 frame %s: registers=%u int-vreg=%u reference-vreg=%u child-invoke-DexPc=%u\n",names[i],frames[i].registers,frames[i].integer_vreg,frames[i].reference_vreg,frames[i].invoke_dex_pc);}
