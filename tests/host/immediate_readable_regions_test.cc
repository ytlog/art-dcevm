#include "platform/common/readable_mapping_scope.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/proc_mapping_region.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm::method_identity;
static void Parser(){
 using namespace artdcevm::proc_mapping_region;Region r;
 for(unsigned mode=0;mode<16;++mode){char line[160];
  const int n=std::snprintf(line,sizeof(line),"1000-2000 %c%c%c%c 0000 00:00 0 /spaces truncated",mode&1?'r':'-',mode&2?'w':'-',mode&4?'x':'-',mode&8?'s':'p');
  assert(n>0&&Parse(line,size_t(n),&r));assert(r.begin==0x1000&&r.end==0x2000&&r.readable==bool(mode&1)&&r.writable==bool(mode&2));
 }
 for(const char*bad:{"2000-1000 r--p ","1000-2000 r--pX","1000-2000 q--p ","1000-2000 r--p","10000000000000000-2000 r--p "})assert(!Parse(bad,std::strlen(bad),&r));
 const char zero[]="1000-1000 rwxp ";assert(Parse(zero,sizeof(zero)-1,&r)&&!r.readable&&!r.writable);
}
int main(){
 Parser();const size_t page=size_t(sysconf(_SC_PAGESIZE));assert(page);
 const size_t regions=ImmediateReadableRegions::kCapacity+1,pages=regions*2+1;
 auto*area=static_cast<unsigned char*>(mmap(nullptr,pages*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(area!=MAP_FAILED);
 for(size_t i=0;i<regions;++i)assert(mprotect(area+i*2*page,page,PROT_READ|PROT_WRITE)==0);
 size_t extent=999;
 const auto before=MappingOsQueries();
 {ImmediateReadableRegions read;assert(read.OsQueries()==0);
  assert(!read.Extent(nullptr,&extent)&&extent==0);assert(!read.Extent(area,nullptr));assert(read.OsQueries()==0);
  assert(read.Extent(area,&extent)&&extent==page);assert(read.OsQueries()==1);
  for(size_t i=0;i<page;++i)assert(read.Extent(area+i,&extent)&&extent==page-i);
  assert(read.OsQueries()==1);assert(!read.Extent(area+page,&extent)&&extent==0);assert(read.OsQueries()==2);
  assert(!read.Extent(reinterpret_cast<void*>(1),&extent)&&extent==0);assert(read.OsQueries()==3);
  for(size_t i=1;i<regions;++i)assert(read.Extent(area+i*2*page,&extent)&&extent==page);
  assert(read.OsQueries()==regions+2);
  // Seventeen independently separated VMAs evict the first of sixteen slots.
  const auto q=read.OsQueries();assert(read.Extent(area,&extent)&&extent==page);assert(read.OsQueries()==q+1);
  assert(MappingOsQueries()-before==read.OsQueries());
 }
 // Change permissions only AFTER destroying the native interval. A new reader
 // must inspect current OS truth, never reuse an earlier positive or negative.
 assert(mprotect(area,page,PROT_NONE)==0);
 {ImmediateReadableRegions read;assert(!read.Extent(area,&extent)&&extent==0&&read.OsQueries()==1);}
 assert(mprotect(area,page,PROT_READ)==0);
 {ImmediateReadableRegions read;assert(read.Extent(area,&extent)&&extent==page&&read.OsQueries()==1);}
 // Explicit readers neither install nor bypass the existing full TLS snapshot.
 {ScopedReadableMappings full;assert(full.Ready());const auto q=full.OsQueries();
  ImmediateReadableRegions immediate;assert(immediate.Extent(area,&extent)&&extent==page&&immediate.OsQueries()==1);
  assert(full.Extent(area,&extent)&&extent==page&&full.OsQueries()==q);
 }
 assert(munmap(area,pages*page)==0);
 {ImmediateReadableRegions read;assert(!read.Extent(area,&extent)&&extent==0&&read.OsQueries()==1);}
 std::puts("Immediate readable regions PASS: actual OS permissions/bounds, same-window hits, FIFO overflow recapture, invalid/null/guard pages, fresh permission/unmap changes, exact query counts, isolated full snapshot and Linux parser modes");
}
