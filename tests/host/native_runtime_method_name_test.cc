#include "platform/common/native_runtime_frame.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <cstdio>
bool noalloc=false;
void*operator new(size_t n){assert(!noalloc);if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
namespace {
using namespace artdcevm;
int runtime_method;const char literal[]="<runtime internal callee-save reference and argument registers method>";
char long_literal[129];const char*returned=literal;size_t named_calls=0;
const char*Name(void*method){assert(noalloc&&method==&runtime_method);++named_calls;return returned;}
struct Bounds{const void*base;size_t bytes;bool live=true;size_t calls=0;};
bool Extent(void*raw,const void*p,size_t*out){auto&b=*static_cast<Bounds*>(raw);++b.calls;const auto at=uintptr_t(p),base=uintptr_t(b.base);if(!b.live||at<base||at-base>=b.bytes)return false;*out=b.bytes-(at-base);return true;}
}
int main(){
 using namespace artdcevm::native_runtime_frame;
 auto source=Service::Bind([](const char*symbol)->void*{return !std::strcmp(symbol,Service::kRuntimeName)?reinterpret_cast<void*>(Name):nullptr;});
 auto missing=Service::Bind([](const char*)->void*{return nullptr;});
 Bounds bounds{literal,sizeof(literal)};modern_field_metadata::ReadContext read{&bounds,Extent};
 char copied[128]{};noalloc=true;
 assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::Complete);
 assert(!std::strcmp(copied,literal)&&named_calls==1&&bounds.calls==1);
 bounds.live=false;assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::UnmappedResult&&!copied[0]);bounds.live=true;
 assert(source.CopyObservedName(&runtime_method,nullptr,copied,sizeof(copied))==NameStatus::UnmappedResult&&!copied[0]);
 returned=nullptr;assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::NullResult&&!copied[0]);
 // A returned pointer outside prepared bounds is never dereferenced.
 returned=reinterpret_cast<const char*>(uintptr_t(0x1234));assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::UnmappedResult&&!copied[0]);
 std::memset(long_literal,'x',sizeof(long_literal));long_literal[128]=0;returned=long_literal;bounds={long_literal,sizeof(long_literal)};
 assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::Truncated&&!copied[0]);
 long_literal[127]=0;assert(source.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::Complete&&std::strlen(copied)==127);
 const auto calls=named_calls;assert(missing.CopyObservedName(&runtime_method,&read,copied,sizeof(copied))==NameStatus::MissingReader&&named_calls==calls&&!copied[0]);
 noalloc=false;puts("Named runtime method literal diagnostic: exact function/receiver, bounded noalloc copy, missing/unmapped/null/truncated rejection PASS; no runtime permission granted");
}
