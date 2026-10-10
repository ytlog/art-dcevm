#include "platform/common/stopped_mapping_reader.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
namespace {bool forbidden=false;size_t allocations=0;}
void* operator new(size_t n){assert(!forbidden);++allocations;if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
using namespace artdcevm;using stopped_mapping_reader::Failure;
struct Input {
 const std::string& bytes;size_t chunk=4096,offset=0,calls=0,fail_call=SIZE_MAX;int saved_errno=0;
 ptrdiff_t operator()(char* out,size_t capacity)noexcept{
  if(calls++==fail_call){saved_errno=EINTR;return -1;}
  const auto count=std::min({bytes.size()-offset,capacity,chunk});
  if(count)std::memcpy(out,bytes.data()+offset,count);offset+=count;return ptrdiff_t(count);
 }
};
struct Sink {
 std::array<proc_mapping_region::Region,16> regions{};size_t count=0,capacity=16;bool refuse=false;
 Failure operator()(const proc_mapping_region::Region& region)noexcept{
  if(refuse)return Failure::Sink;
  if(count==capacity)return Failure::Capacity;
  if(count&&region.begin<regions[count-1].end)return Failure::Overlap;
  regions[count++]=region;return Failure::None;
 }
};
stopped_mapping_reader::Result Run(Input& input,Sink& sink){
 const auto before=allocations;forbidden=true;const auto result=stopped_mapping_reader::Read(input,sink);forbidden=false;
 assert(allocations==before);return result;
}
int main(){
 static_assert(stopped_mapping_reader::Name(Failure::Read)[0]=='r');
 assert(!std::strcmp(stopped_mapping_reader::Name(Failure::Capacity),"capacity")&&!std::strcmp(stopped_mapping_reader::Name(Failure::Parse),"parse"));
 const std::string complete="1000-2000 r--p 00000000 00:00 0 /a path\n2000-3000 ---p 00000000 00:00 0\n3000-4000 rw-p 00000000 00:00 0\n";
 for(size_t chunk:{size_t(1),size_t(7),size_t(4096)}){
  Input input{complete,chunk};Sink sink;auto r=Run(input,sink);
  assert(r.failure==Failure::None&&r.lines==3&&r.readable_regions==2&&r.bytes==complete.size()&&sink.count==2&&r.failure_prefix_length==0);
  assert(sink.regions[0].begin==0x1000&&sink.regions[1].writable);
 }
 std::string large=complete;for(size_t n=0;n<512;++n)large+="5000-6000 ---p 00000000 00:00 0\n";
 {Input i{large};Sink s;auto r=Run(i,s);assert(r.failure==Failure::None&&r.lines==515&&r.readable_regions==2&&r.bytes==large.size()&&i.calls>2);}
 const std::string partial="1000-2000 r--p ";{Input i{partial};Sink s;auto r=Run(i,s);assert(r.failure==Failure::None&&r.lines==1&&s.count==1);}
 const std::string truncated="1000-2000 r-";{Input i{truncated,2};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Parse&&s.count==0&&r.failure_prefix_length==truncated.size()&&!std::memcmp(r.failure_prefix,truncated.data(),truncated.size()));}
 {Input i{complete,7,0,0,2};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Read&&i.calls==3&&i.saved_errno==EINTR);}
 const std::string malformed="1000-2000 garbage\n";{Input i{malformed};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Parse&&s.count==0&&r.failure_prefix_length==malformed.size()-1&&!std::memcmp(r.failure_prefix,malformed.data(),r.failure_prefix_length));}
 std::string bad_tail="invalid";bad_tail.resize(119,'x');bad_tail[17]='\0';bad_tail+='\n';
 const std::string multiblock=large+bad_tail;
 {Input i{multiblock,7};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Parse&&r.lines==516&&r.line_length==119&&r.failure_prefix_length==64&&!std::memcmp(r.failure_prefix,bad_tail.data(),64));}
 const std::string embedded_newline="1000-2000 r--p \ninvalid\n";
 {Input i{embedded_newline,1};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Parse&&r.lines==2&&r.failure_prefix_length==7&&!std::memcmp(r.failure_prefix,"invalid",7));}

 std::string exact="1000-2000 r--p ";exact.resize(1024,'x');exact+='\n';{Input i{exact,13};Sink s;assert(Run(i,s).failure==Failure::None&&s.count==1);}
 std::string long_line="1000-2000 r--p ";long_line.resize(1025,'x');long_line+='\n';{Input i{long_line,13};Sink s;auto r=Run(i,s);assert(r.failure==Failure::LineTooLong&&r.line_length==1025&&s.count==0);}
 // Exact failed Android16 kernel prefix is syntactically valid but names no
 // readable/writable bytes; the stopped sink must never receive that record.
 const std::string actual_zero="7b68a72000-7b68a72000 rwxp 0002d000 fe:43 106\n";
 {Input i{actual_zero,7};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Empty&&r.lines==1&&!r.readable_regions&&s.count==0&&r.failure_prefix_length==0);}
 const std::string mixed="7b68a71000-7b68a72000 r--p 0002c000 fe:43 106\n"+actual_zero+"7b68a72000-7b68a73000 rwxp 0002d000 fe:43 106\n";
 {Input i{mixed,1};Sink s;auto r=Run(i,s);assert(r.failure==Failure::None&&r.lines==3&&r.readable_regions==2&&s.count==2&&s.regions[0].end==s.regions[1].begin);}
 const std::string all_zero=actual_zero+actual_zero;
 {Input i{all_zero};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Empty&&r.lines==2&&!r.readable_regions&&s.count==0);}
 const std::string reversed="7b68a72000-7b68a71000 rwxp 0002d000 fe:43 106\n";
 {Input i{reversed};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Parse&&!r.readable_regions&&s.count==0);}
 const std::string empty;{Input i{empty};Sink s;assert(Run(i,s).failure==Failure::Empty&&i.calls==1);}
 const std::string unreadable="1000-2000 ---p \n";{Input i{unreadable};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Empty&&r.lines==1&&s.count==0);}
 {Input i{complete};Sink s;s.capacity=1;auto r=Run(i,s);assert(r.failure==Failure::Capacity&&r.readable_regions==1&&s.count==1);}
 const std::string overlap="1000-3000 r--p \n2000-4000 rw-p \n";{Input i{overlap};Sink s;auto r=Run(i,s);assert(r.failure==Failure::Overlap&&r.readable_regions==1&&s.count==1);}
 {Input i{complete};Sink s;s.refuse=true;assert(Run(i,s).failure==Failure::Sink&&s.count==0);}
 const std::string prefix_missing="\n";{Input i{prefix_missing};Sink s;assert(Run(i,s).failure==Failure::Parse);}
 Sink sink;const auto before=allocations;forbidden=true;auto invalid=[](char*,size_t size)noexcept{return ptrdiff_t(size+1);};auto result=stopped_mapping_reader::Read(invalid,sink);forbidden=false;assert(result.failure==Failure::Read&&allocations==before);
 std::cout<<"Allocation-free stopped mapping stream: complete/cross-chunk/EOF-tail/truncation/read EINTR/no retry/invalid prefix/1024 boundary/long line/empty/capacity/overlap/sink refusal PASS\n";
}
