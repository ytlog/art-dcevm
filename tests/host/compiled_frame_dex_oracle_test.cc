#include "platform/common/compiled_frame_dex_oracle.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace artdcevm::compiled_frame_dex_oracle;
namespace {
void U16(std::vector<unsigned char>& d,size_t p,uint16_t v){d[p]=v;d[p+1]=v>>8;}
void U32(std::vector<unsigned char>& d,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)d[p+i]=v>>(8*i);}
void Uleb(std::vector<unsigned char>& d,size_t& p,uint32_t v){do{auto b=static_cast<unsigned char>(v&127);v>>=7;d[p++]=b|(v?128:0);}while(v);}
std::vector<unsigned char> Fixture(){std::vector<unsigned char> d(512);std::memcpy(d.data(),"dex\n039",8);U32(d,32,d.size());U32(d,36,112);U32(d,40,0x12345678);U32(d,88,4);U32(d,92,128);U32(d,96,1);U32(d,100,160);for(unsigned i=0;i<4;++i)U16(d,128+i*8,7);U32(d,160,7);U32(d,184,192);size_t p=192;for(auto n:{0,0,4,0})Uleb(d,p,n);for(unsigned i=0;i<4;++i){Uleb(d,p,i?1:0);Uleb(d,p,i==3?0x109:9);Uleb(d,p,i==3?0:256+i*64);}for(unsigned i=0;i<3;++i){size_t c=256+i*64;U16(d,c,8);U16(d,c+2,6);U16(d,c+4,6);U32(d,c+12,4);U16(d,c+16,0x71);U16(d,c+18,i+1);U16(d,c+22,0x0f);}return d;}
}
int main(){auto d=Fixture();std::array<Frame,3> output{};std::array<uint32_t,4> methods{0,1,2,3};auto read=[&]{return Read(d.data(),d.size(),methods,&output);};assert(read());for(unsigned i=0;i<3;++i){assert(output[i].registers==8&&output[i].outs==6&&output[i].integer_vreg==2+i*2&&output[i].reference_vreg==3+i*2&&output[i].invoke_dex_pc==0);}const auto baseline=output;
 U16(d,256,17);assert(!read());U16(d,256,8);
 U16(d,256+2,5);assert(!read());U16(d,258,6);
 U16(d,274,3);assert(!read());U16(d,274,1);
 U16(d,272,0x100);assert(!read());U16(d,272,0x71);
 U16(d,278,0x73);assert(!read());U16(d,278,0x0f);
 U32(d,268,2);assert(!read());U32(d,268,4);
 U32(d,268,7);U16(d,278,0x71);U16(d,280,1);U16(d,284,0x0f);assert(!read());U32(d,268,4);U16(d,278,0x0f);
 methods[3]=2;assert(!read());methods[3]=3;
 d[6]='1';d[5]='4';assert(!read());d[5]='3';d[6]='9';
 for(size_t n:{0u,111u,255u,383u})assert(!Read(d.data(),n,methods,&output));
 assert(read());assert(output[2].reference_vreg==baseline[2].reference_vreg);
 std::puts("Independent installed DEX frame CodeItems: three exact incoming int/reference vregs and child invoke PCs; malformed/truncated/ambiguous/payload/unknown opcode/wrong registers rejected");}
