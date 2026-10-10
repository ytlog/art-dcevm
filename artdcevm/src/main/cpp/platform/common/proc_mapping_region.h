#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <system_error>
namespace artdcevm::proc_mapping_region {
struct Region {uintptr_t begin=0,end=0;bool readable=false,writable=false;};
// Only the kernel's fixed address/permission prefix is relevant. File names,
// including spaces or truncated tails, cannot change the mapped permissions.
inline bool Parse(const char* line,size_t length,Region* out)noexcept {
 if(!out)return false;*out={};if(!line||!length)return false;
 const char* end=line+length;uintptr_t begin=0,limit=0;
 const auto first=std::from_chars(line,end,begin,16);
 if(first.ec!=std::errc{}||first.ptr==end||*first.ptr!='-')return false;
 const auto second=std::from_chars(first.ptr+1,end,limit,16);
 if(second.ec!=std::errc{}||second.ptr==end||*second.ptr!=' '||limit<=begin)return false;
 const char* mode=second.ptr+1;
 if(end-mode<5||(mode[0]!='r'&&mode[0]!='-')||(mode[1]!='w'&&mode[1]!='-')||
    (mode[2]!='x'&&mode[2]!='-')||(mode[3]!='p'&&mode[3]!='s')||mode[4]!=' ')return false;
 *out={begin,limit,mode[0]=='r',mode[1]=='w'};return true;
}
}
