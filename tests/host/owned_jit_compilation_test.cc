#include "platform/common/owned_jit_compilation.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace artdcevm::owned_jit_compilation;
namespace {
int jit,method,thread;bool runnable=true,pending=false;unsigned queues=0,drains=0;
void Queue(void* j,void* m,void* t){assert(j==&jit&&m==&method&&t==&thread&&runnable);pending=true;++queues;}
void Wait(void* j,void* t){assert(j==&jit&&t==&thread&&!runnable&&pending);pending=false;++drains;}
void* Find(const char* name){return !std::strcmp(name,kEnqueue)?reinterpret_cast<void*>(Queue):!std::strcmp(name,kWait)?reinterpret_cast<void*>(Wait):nullptr;}
}
int main(){
 Service service;assert(!service.Queue(&jit,&method,&thread));assert(!service.Drain(&jit,&thread));
 assert(Service::Bind(Find,&service));
 assert(!Service::Bind([](const char* name){return !std::strcmp(name,kWait)?nullptr:Find(name);},&service));
 assert(!service.Queue(nullptr,&method,&thread));assert(!service.Queue(&jit,nullptr,&thread));assert(!service.Queue(&jit,&method,nullptr));
 assert(queues==0&&drains==0);assert(service.Queue(&jit,&method,&thread)&&pending&&queues==1);
 runnable=false;assert(!service.Drain(nullptr,&thread));assert(!service.Drain(&jit,nullptr));
 assert(service.Drain(&jit,&thread)&&!pending&&drains==1);
 puts("Owned JIT compilation: exact queue/drain ABI, independently supplied identities, distinct lock phases, missing binding preservation and null-call rejection passed");
}
