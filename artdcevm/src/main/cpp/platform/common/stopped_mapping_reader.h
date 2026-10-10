#pragma once
#include "platform/common/proc_mapping_region.h"
#include <cstddef>
namespace artdcevm::stopped_mapping_reader {
enum class Failure {None,Read,LineTooLong,Parse,Capacity,Overlap,Empty,Sink};
constexpr const char* Name(Failure failure) noexcept {
 switch(failure){
  case Failure::None:return "none";
  case Failure::Read:return "read";
  case Failure::LineTooLong:return "line-too-long";
  case Failure::Parse:return "parse";
  case Failure::Capacity:return "capacity";
  case Failure::Overlap:return "overlap";
  case Failure::Empty:return "empty";
  case Failure::Sink:return "sink";
 }
 return "unknown";
}
struct Result {
 Failure failure=Failure::None;
 size_t readable_regions=0,lines=0,bytes=0,line_length=0;
 char failure_prefix[64]{};size_t failure_prefix_length=0;
};
// Caller owns the actual stopped token, fd and preallocated sink. Reader has
// signature ptrdiff_t(char*,size_t), returning 0 for EOF or negative on failure;
// capture errno in that callback if needed. No retry or partial grant occurs.
// Sink accepts one readable Region and returns Failure::None or its precise
// refusal (e.g. Capacity/Overlap). This helper grants no mapping/read authority.
template<class Reader,class Sink> Result Read(Reader&& reader,Sink&& sink) noexcept {
 Result result;char buffer[4096],line[1024];size_t used=0;
 const auto consume=[&]() noexcept {
  result.line_length=used;++result.lines;proc_mapping_region::Region region;
  if(!proc_mapping_region::Parse(line,used,&region)){
   result.failure=Failure::Parse;result.failure_prefix_length=used<sizeof(result.failure_prefix)?used:sizeof(result.failure_prefix);
   for(size_t n=0;n<result.failure_prefix_length;++n)result.failure_prefix[n]=line[n];
   return false;
  }
  if(!region.readable)return true;
  const auto failure=sink(region);
  if(failure!=Failure::None){result.failure=failure;return false;}
  ++result.readable_regions;return true;
 };
 for(;;){
  const auto count=reader(buffer,sizeof(buffer));
  if(count<0||static_cast<size_t>(count)>sizeof(buffer)){result.failure=Failure::Read;return result;}
  if(!count)break;
  result.bytes+=static_cast<size_t>(count);
  for(size_t n=0;n<static_cast<size_t>(count);++n){
   if(buffer[n]=='\n'){if(!consume())return result;used=0;}
   else if(used==sizeof(line)){result.line_length=used+1;result.failure=Failure::LineTooLong;return result;}
   else line[used++]=buffer[n];
  }
 }
 if(used&&!consume())return result;
 if(!result.readable_regions)result.failure=Failure::Empty;
 return result;
}
}
