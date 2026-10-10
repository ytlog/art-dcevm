#include "platform/common/readable_mapping_scope.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/proc_mapping_region.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <fstream>
#include <string>
using namespace artdcevm::method_identity;
static void KernelPrefixes(){
 using artdcevm::proc_mapping_region::Region;
 using artdcevm::proc_mapping_region::Parse;
 Region region;
 for(unsigned permissions=0;permissions<16;++permissions){
  const char mode[]={permissions&1?'r':'-',permissions&2?'w':'-',permissions&4?'x':'-',permissions&8?'s':'p',0};
  char line[160];const int length=std::snprintf(line,sizeof(line),"7b12345000-7b12348000 %s 00000000 00:00 0 /a file (deleted)\n",mode);
  assert(length>0&&Parse(line,size_t(length),&region));
  assert(region.begin==UINT64_C(0x7b12345000)&&region.end==UINT64_C(0x7b12348000));
  assert(region.readable==bool(permissions&1)&&region.writable==bool(permissions&2));
  char zero[160];const int zero_length=std::snprintf(zero,sizeof(zero),"7b68a72000-7b68a72000 %s 0002d000 fe:43 106",mode);
  assert(zero_length>0&&Parse(zero,size_t(zero_length),&region));
  assert(region.begin==UINT64_C(0x7b68a72000)&&region.end==region.begin&&!region.readable&&!region.writable);
  // A truncated pathname is permitted only after the complete mode prefix.
  const auto* suffix=std::strstr(line,"00000000");assert(suffix&&Parse(line,size_t(suffix-line),&region));
 }
 const char* invalid[]={"","1-2 r--p","1-2 r--pX","1-2 q--p ","1-2 rQ-p ","1-2 r-Qp ","1-2 r--Q ","2-1 r--p ","-2 r--p ","1- r--p "," 1-2 r--p ","+1-2 r--p ","0x1-2 r--p ","1-10000000000000000 r--p ","10000000000000000-2 r--p ","1\n-2 r--p "};
 for(const char* line:invalid){region={1,2,true,true};assert(!Parse(line,std::strlen(line),&region));assert(!region.begin&&!region.end&&!region.readable&&!region.writable);}
 assert(!Parse(nullptr,0,&region)&&!Parse("1-2 r--p ",10,nullptr));
 const char maximum[]="FFFFFFFFFFFFFFFE-FFFFFFFFFFFFFFFF r--p ";assert(Parse(maximum,sizeof(maximum)-1,&region)&&region.end==UINTPTR_MAX);
 std::puts("Kernel map prefixes: 16 permission combinations, filename/truncation independence, zero-length no-grant, malformed/overflow/reversed ranges and stale-output revocation PASS");
}
static void CapturedKernelMappings(const char* path){
 std::ifstream source(path);assert(source.is_open());std::string line;size_t count=0;
 while(std::getline(source,line)){
  unsigned long long begin=0,end=0;char mode[5]{};
  assert(std::sscanf(line.c_str(),"%llx-%llx %4s",&begin,&end,mode)==3);
  artdcevm::proc_mapping_region::Region region;
  assert(artdcevm::proc_mapping_region::Parse(line.data(),line.size(),&region));
  assert(region.begin==begin&&region.end==end&&region.readable==(begin<end&&mode[0]=='r')&&region.writable==(begin<end&&mode[1]=='w'));++count;
 }
 assert(source.eof()&&count>0);std::printf("Supplied kernel mapping records: %zu exact ranges/read/write modes agree with independent formatted-parser oracle PASS\n",count);
}
int main(int argc,char** argv){
 assert(argc<=2);if(argc==2)CapturedKernelMappings(argv[1]);
 KernelPrefixes();
 std::vector<unsigned char> classes(60000*256);size_t extent=0;
 const auto before=MappingOsQueries();
 {
  ScopedReadableMappings scope;assert(scope.Ready());
  const auto captured=MappingOsQueries();assert(captured>before);
  for(size_t count:{size_t(30000),size_t(60000)})for(size_t i=0;i<count;++i){
   const auto*address=classes.data()+i*256;
   for(int read=0;read<21;++read)assert(scope.Extent(address,&extent)&&extent>=256);
  }
  assert(MappingOsQueries()==captured);
  extent=99;assert(!scope.Extent(reinterpret_cast<void*>(1),&extent)&&!extent);
  assert(!scope.Extent(nullptr,&extent)&&!scope.Extent(classes.data(),nullptr));
  assert(MappingOsQueries()==captured);
  {
   ScopedReadableMappings nested;assert(nested.Ready());auto count=MappingOsQueries();
   assert(!scope.Extent(classes.data(),&extent));
   assert(nested.Extent(classes.data(),&extent));assert(MappingOsQueries()==count);
  }
  assert(scope.Extent(classes.data(),&extent));
 }
 // A fresh traversal captures after a possible safepoint; it does not retain
 // the previous snapshot or reuse a negative lookup from an earlier window.
 const auto next=MappingOsQueries();{ScopedReadableMappings scope;assert(scope.Ready()&&scope.Extent(classes.data(),&extent));assert(MappingOsQueries()>next);}
 std::puts("Actual ordinary mapping snapshot: full30k/60k x21 reads, zero per-node OS queries, denied pointer no fallback, nested inactive view refusal and fresh traversal recapture PASS");
}
